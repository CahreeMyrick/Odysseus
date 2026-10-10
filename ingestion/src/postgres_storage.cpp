#include "postgres_storage.hpp"
#include "model_json.hpp"
#include "schema_sql.hpp"

#include <libpq-fe.h>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace odysseus::ingestion {
namespace {
using Connection = std::unique_ptr<PGconn, decltype(&PQfinish)>;
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;

Result run(PGconn* connection, const char* sql,
             const std::vector<std::string>& parameters = {}) {
    std::vector<const char*> values;
    for (const auto& parameter : parameters) {
        if (parameter.find('\0') != std::string::npos) {
            throw std::invalid_argument("PostgreSQL text values cannot contain NUL bytes");
        }
        values.push_back(parameter.c_str());
    }
    Result result(parameters.empty()
        ? PQexec(connection, sql)
        : PQexecParams(connection, sql, static_cast<int>(values.size()), nullptr,
                       values.data(), nullptr, nullptr, 0), &PQclear);
    if (!result || (PQresultStatus(result.get()) != PGRES_COMMAND_OK &&
                    PQresultStatus(result.get()) != PGRES_TUPLES_OK)) {
        // Avoid echoing data, credentials, or SQL values from server diagnostics.
        const char* state = result ? PQresultErrorField(result.get(), PG_DIAG_SQLSTATE) : nullptr;
        throw std::runtime_error("PostgreSQL command failed (SQLSTATE " +
                                 std::string(state ? state : "unavailable") + ")");
    }
    return result;
}

void execute(PGconn* connection, const char* sql,
             const std::vector<std::string>& parameters = {}) {
    (void)run(connection, sql, parameters);
}

class Transaction {
public:
    explicit Transaction(PGconn* connection) : connection_(connection) {
        execute(connection_, "BEGIN");
    }
    ~Transaction() {
        if (!committed_) {
            Result rollback(PQexec(connection_, "ROLLBACK"), &PQclear);
        }
    }
    void commit() {
        execute(connection_, "COMMIT");
        committed_ = true;
    }
private:
    PGconn* connection_;
    bool committed_ = false;
};
}

struct PostgresStorage::Impl {
    Connection connection;
    explicit Impl(const std::string& connection_string)
        : connection(PQconnectdb(connection_string.c_str()), &PQfinish) {
        if (!connection || PQstatus(connection.get()) != CONNECTION_OK) {
            throw std::runtime_error("Unable to connect to PostgreSQL; check ODYSSEUS_DATABASE_URL");
        }
        if (PQsetClientEncoding(connection.get(), "UTF8") != 0) {
            throw std::runtime_error("Unable to set PostgreSQL client encoding to UTF-8");
        }
    }
};

PostgresStorage::PostgresStorage(const std::string& connection_string)
    : impl_(std::make_unique<Impl>(connection_string)) {}
PostgresStorage::~PostgresStorage() = default;

void PostgresStorage::initialize_schema() {
    Transaction transaction(impl_->connection.get());
    execute(impl_->connection.get(), initial_schema_sql);
    transaction.commit();
}

void PostgresStorage::write(const StructuredModel& structured) {
    const auto& artifact = structured.artifact;
    if (artifact.id.empty()) throw std::invalid_argument("Artifact ID must not be empty");
    if (artifact.content.encoding != ContentEncoding::Utf8 || artifact.content.has_bytes()) {
        throw std::invalid_argument("PostgresStorage currently supports UTF-8 text artifacts only");
    }
    const bool is_code = std::holds_alternative<code::FileModel>(structured.model);
    if (artifact.modality != (is_code ? Modality::Code : Modality::Document)) {
        throw std::invalid_argument("Artifact modality and processed model type disagree");
    }
    const json metadata = {
        {"name", artifact.name}, {"type", artifact.type}, {"modality", artifact.modality},
        {"mime_type", artifact.content.mime_type}, {"encoding", artifact.content.encoding},
        {"content_hash", artifact.content_hash}, {"size_bytes", artifact.size_bytes},
        {"location", artifact.location}, {"provenance", artifact.provenance},
        {"metadata", artifact.metadata}
    };
    const auto model_json = std::visit([](const auto& model) { return json(model).dump(); },
                                     structured.model);
    Transaction transaction(impl_->connection.get());
    execute(impl_->connection.get(), R"sql(
        INSERT INTO odysseus_ingestion.artifacts
            (id, source_uri, revision, path, content, metadata)
        VALUES ($1, $2, $3, $4, $5, $6::jsonb)
        ON CONFLICT (id) DO UPDATE SET
            source_uri = EXCLUDED.source_uri, revision = EXCLUDED.revision,
            path = EXCLUDED.path, content = EXCLUDED.content, metadata = EXCLUDED.metadata,
            updated_at = CURRENT_TIMESTAMP
    )sql", {artifact.id, artifact.provenance.source_id, artifact.provenance.snapshot_id,
            artifact.location.path, artifact.content.text, metadata.dump()});
    execute(impl_->connection.get(), R"sql(
        INSERT INTO odysseus_ingestion.models (artifact_id, model_type, model)
        VALUES ($1, $2, $3::jsonb)
        ON CONFLICT (artifact_id) DO UPDATE SET
            model_type = EXCLUDED.model_type, model = EXCLUDED.model,
            updated_at = CURRENT_TIMESTAMP
    )sql", {artifact.id, is_code ? "code" : "document", model_json});
    // Save the exact input for workers; retries never need to re-fetch GitHub.
    execute(impl_->connection.get(), R"sql(
        INSERT INTO odysseus_ingestion.indexing_tasks AS tasks (artifact_id, target, payload)
        SELECT a.id, target, jsonb_build_object(
            'source_uri', a.source_uri, 'revision', a.revision, 'path', a.path,
            'content', a.content, 'metadata', a.metadata,
            'model_type', m.model_type, 'model', m.model, 'projection_version', 1)
        FROM odysseus_ingestion.artifacts a
        JOIN odysseus_ingestion.models m ON m.artifact_id = a.id
        CROSS JOIN (VALUES ('vector'), ('graph')) targets(target)
        WHERE a.id = $1
        ON CONFLICT (artifact_id, target) DO UPDATE SET
            payload = EXCLUDED.payload, generation = tasks.generation + 1,
            status = 'pending', attempts = 0, claim_token = NULL, lease_until = NULL,
            available_at = CURRENT_TIMESTAMP, last_error = NULL, updated_at = CURRENT_TIMESTAMP
        WHERE tasks.payload IS DISTINCT FROM EXCLUDED.payload
    )sql", {artifact.id});
    transaction.commit();
}

std::optional<IndexingTask> PostgresStorage::claim_task(IndexTarget target, int lease_seconds) {
    if (lease_seconds <= 0) throw std::invalid_argument("Lease duration must be positive");
    auto result = run(impl_->connection.get(), R"sql(
        WITH candidate AS (
            SELECT id FROM odysseus_ingestion.indexing_tasks
            WHERE target = $1 AND (
                (status IN ('pending','failed') AND available_at <= CURRENT_TIMESTAMP)
                OR (status = 'running' AND lease_until <= CURRENT_TIMESTAMP))
            ORDER BY available_at, id FOR UPDATE SKIP LOCKED LIMIT 1
        )
        UPDATE odysseus_ingestion.indexing_tasks t SET status = 'running',
            attempts = attempts + 1,
            claim_token = nextval('odysseus_ingestion.indexing_claim_tokens'),
            lease_until = CURRENT_TIMESTAMP + $2::int * interval '1 second',
            updated_at = CURRENT_TIMESTAMP
        FROM candidate c WHERE t.id = c.id
        RETURNING t.id, t.claim_token, t.generation, t.attempts, t.artifact_id, t.payload::text
    )sql", {target == IndexTarget::Vector ? "vector" : "graph", std::to_string(lease_seconds)});
    if (PQntuples(result.get()) == 0) return std::nullopt;
    return IndexingTask{std::stoll(PQgetvalue(result.get(),0,0)),
        std::stoll(PQgetvalue(result.get(),0,1)), std::stoll(PQgetvalue(result.get(),0,2)),
        std::stoi(PQgetvalue(result.get(),0,3)), PQgetvalue(result.get(),0,4), target,
        json::parse(PQgetvalue(result.get(),0,5))};
}

bool PostgresStorage::complete_task(const IndexingTask& task) {
    auto result = run(impl_->connection.get(), R"sql(
        UPDATE odysseus_ingestion.indexing_tasks SET status='completed',
            lease_until=NULL, claim_token=NULL, last_error=NULL, updated_at=CURRENT_TIMESTAMP
        WHERE id=$1::bigint AND claim_token=$2::bigint AND status='running'
            AND lease_until > CURRENT_TIMESTAMP RETURNING id
    )sql", {std::to_string(task.id), std::to_string(task.claim_token)});
    return PQntuples(result.get()) == 1;
}

bool PostgresStorage::fail_task(const IndexingTask& task, const std::string& safe_error,
                              int retry_delay_seconds) {
    if (retry_delay_seconds < 0) throw std::invalid_argument("Retry delay must not be negative");
    auto result = run(impl_->connection.get(), R"sql(
        UPDATE odysseus_ingestion.indexing_tasks SET status='failed',
            lease_until=NULL, claim_token=NULL, last_error=$3,
            available_at=CURRENT_TIMESTAMP + $4::int * interval '1 second',
            updated_at=CURRENT_TIMESTAMP
        WHERE id=$1::bigint AND claim_token=$2::bigint AND status='running'
            AND lease_until > CURRENT_TIMESTAMP RETURNING id
    )sql", {std::to_string(task.id), std::to_string(task.claim_token), safe_error.substr(0, 2000),
             std::to_string(retry_delay_seconds)});
    return PQntuples(result.get()) == 1;
}

bool PostgresStorage::renew_task(const IndexingTask& task, int lease_seconds) {
    if (lease_seconds <= 0) throw std::invalid_argument("Lease duration must be positive");
    auto result = run(impl_->connection.get(), R"sql(
        UPDATE odysseus_ingestion.indexing_tasks SET
            lease_until=CURRENT_TIMESTAMP + $3::int * interval '1 second',
            updated_at=CURRENT_TIMESTAMP
        WHERE id=$1::bigint AND claim_token=$2::bigint AND status='running'
            AND lease_until > CURRENT_TIMESTAMP RETURNING id
    )sql", {std::to_string(task.id), std::to_string(task.claim_token), std::to_string(lease_seconds)});
    return PQntuples(result.get()) == 1;
}

} // namespace odysseus::ingestion
