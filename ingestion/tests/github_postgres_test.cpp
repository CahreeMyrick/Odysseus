#include "rest_github_client.hpp"
#include "postgres_storage.hpp"
#include "content_classifier.hpp"
#include <libpq-fe.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace odysseus::ingestion;
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Result query(PGconn* connection, const char* sql) {
    Result result(PQexec(connection, sql), &PQclear);
    check(result && PQresultStatus(result.get()) == PGRES_TUPLES_OK, "Verification query failed");
    return result;
}
int main() {
    const auto* url = std::getenv("ODYSSEUS_GITHUB_TEST_DATABASE_URL");
    if (!url || !*url) {
        std::cout << "Set ODYSSEUS_GITHUB_TEST_DATABASE_URL to an isolated empty database to enable live GitHub verification\n";
        return 77;
    }
    try {
        std::unique_ptr<PGconn, decltype(&PQfinish)> connection(PQconnectdb(url), PQfinish);
        check(connection && PQstatus(connection.get()) == CONNECTION_OK, "Test database connection failed");
        auto schemas = query(connection.get(), "SELECT count(*) FROM pg_namespace WHERE nspname='odysseus_ingestion'");
        check(std::string(PQgetvalue(schemas.get(), 0, 0)) == "0", "Live test requires an empty ingestion schema");
        const auto* token = std::getenv("GITHUB_TOKEN");
        RestGitHubClient client(std::make_shared<CurlTransport>(), token ? token : "");
        SourceRequest request;
        request.type = SourceType::GitHub;
        request.uri = "https://github.com/octocat/Spoon-Knife";
        const auto expected = client.fetch_repository(request);
        request.options["ref"] = expected.revision; // Pin both ingestions to the same immutable snapshot.
        PostgresStorage storage(url);
        storage.initialize_schema();
        AdapterFactory factory;
        factory.register_source(SourceType::GitHub, [&client](const SourceRequest& source) {
            return std::make_unique<GitHubAdapter>(client, source);
        });
        ArtifactProcessor processor;
        IngestionPipeline pipeline(factory, processor, storage);
        const auto first = pipeline.ingest(request);
        const auto second = pipeline.ingest(request);
        check(first.artifacts_stored > 0, "Live fixture no longer contains supported content");
        check(first.artifacts_stored == second.artifacts_stored && first.artifacts_skipped == second.artifacts_skipped,
              "Pinned ingestion counts changed");
        auto rows = query(connection.get(),
            "SELECT a.path,a.content,a.revision,a.source_uri,m.model_type,m.model::text "
            "FROM odysseus_ingestion.artifacts a JOIN odysseus_ingestion.models m ON m.artifact_id=a.id");
        check(static_cast<std::size_t>(PQntuples(rows.get())) == first.artifacts_stored, "Repeat ingestion duplicated rows");
        std::size_t supported = 0;
        for (const auto& file : expected.files) {
            if (!ContentClassifier::supports_path(file.path)) continue;
            ++supported;
            bool found = false;
            for (int row = 0; row < PQntuples(rows.get()); ++row) {
                if (file.path != PQgetvalue(rows.get(), row, 0)) continue;
                found = true;
                check(file.content == PQgetvalue(rows.get(), row, 1), "Stored content differs from GitHub");
                check(expected.revision == PQgetvalue(rows.get(), row, 2), "Stored commit differs from snapshot");
                check(request.uri == PQgetvalue(rows.get(), row, 3), "Source provenance lost");
                const auto model = nlohmann::json::parse(PQgetvalue(rows.get(), row, 5));
                if (std::string(PQgetvalue(rows.get(), row, 4)) == "document")
                    check(model.at("sections").at(0).at("text") == file.content, "Document model differs from source");
            }
            check(found, "Missing supported file");
        }
        check(supported == first.artifacts_stored && expected.files.size() - supported == first.artifacts_skipped,
              "Stored/skipped counts disagree with snapshot");
        std::cout << "Live GitHub -> PostgreSQL passed: " << supported
                  << " artifacts; content, models, commit provenance, and repeat ingestion verified\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
