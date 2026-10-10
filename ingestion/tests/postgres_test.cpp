#include "mock_github_client.hpp"
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
    check(result && (PQresultStatus(result.get()) == PGRES_TUPLES_OK ||
                     PQresultStatus(result.get()) == PGRES_COMMAND_OK), "Test SQL failed");
    return result;
}
std::string scalar(PGconn* connection, const char* sql) {
    auto result = query(connection, sql);
    check(PQntuples(result.get()) == 1, "Expected one result row");
    return PQgetvalue(result.get(), 0, 0);
}
int main() {
    const char* url = std::getenv("ODYSSEUS_TEST_DATABASE_URL");
    if (!url || !*url) {
        std::cout << "Set ODYSSEUS_TEST_DATABASE_URL to an isolated empty test database\n";
        return 77;
    }
    try {
        std::unique_ptr<PGconn, decltype(&PQfinish)> connection(PQconnectdb(url), &PQfinish);
        check(connection && PQstatus(connection.get()) == CONNECTION_OK, "Test connection failed");
        check(scalar(connection.get(), "SELECT count(*) FROM pg_namespace WHERE nspname = 'odysseus_ingestion'") == "0",
              "Integration test requires a database without the odysseus_ingestion schema");
        PostgresStorage storage(url);
        storage.initialize_schema();
        storage.initialize_schema(); // Startup is repeatable, not destructive.
        auto client = example_repository();
        AdapterFactory factory;
        factory.register_source(SourceType::GitHub, [&client](const SourceRequest& source) {
            return std::make_unique<GitHubAdapter>(client, source);
        });
        ArtifactProcessor processor;
        IngestionPipeline pipeline(factory, processor, storage);
        SourceRequest request;
        request.type = SourceType::GitHub;
        request.uri = "https://github.com/example/mock-repo";
        check(pipeline.ingest(request).artifacts_stored == 4, "First ingest count wrong");
        check(pipeline.ingest(request).artifacts_stored == 4, "Second ingest count wrong");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.artifacts") == "4", "Duplicate artifacts");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.models") == "4", "Duplicate models");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.models WHERE model_type = 'code'") == "2", "Wrong model types");
        check(scalar(connection.get(), "SELECT content FROM odysseus_ingestion.artifacts WHERE path = 'empty.txt'").empty(), "Lost empty content");
        check(scalar(connection.get(), "SELECT model->'sections'->0->>'text' FROM odysseus_ingestion.models m JOIN odysseus_ingestion.artifacts a ON a.id=m.artifact_id WHERE a.path='README.md'") == "# Mock repository\nA small ingestion fixture.\n", "Lost parsed document");
        auto adapter = factory.create(request);
        auto artifact = adapter->next();
        ContentClassifier{}.classify(*artifact);
        auto model = processor.process(*artifact);
        model.artifact.content.text = "// apostrophe ' and Unicode café\n";
        model.artifact.size_bytes = model.artifact.content.text.size();
        auto& code = std::get<odysseus::code::FileModel>(model.model);
        code.includes = {{"example.hpp", 1}};
        code.classes = {{"Example", 2, 3}};
        code.functions = {{"f", "Example::f", "void f()", "void", 4, 5}};
        code.calls = {{"f", "g", 5}};
        storage.write(model);
        check(scalar(connection.get(), "SELECT content FROM odysseus_ingestion.artifacts WHERE path='src/main.cpp'") == model.artifact.content.text, "Parameterized content round trip failed");
        check(scalar(connection.get(), "SELECT model->'functions'->0->>'qualified_name' FROM odysseus_ingestion.models m JOIN odysseus_ingestion.artifacts a ON a.id=m.artifact_id WHERE a.path='src/main.cpp'") == "Example::f", "Lost structured fields");
        // Fail the second write after the first artifact upsert succeeds.
        query(connection.get(), "ALTER TABLE odysseus_ingestion.models ADD CONSTRAINT test_reject CHECK (NOT (model->'file'->'metadata' ? 'reject_write'))");
        model.artifact.content.text = "must be rolled back";
        code.file.metadata["reject_write"] = true;
        bool failed = false;
        try { storage.write(model); } catch (const std::runtime_error&) { failed = true; }
        check(failed, "Expected model write failure");
        check(scalar(connection.get(), "SELECT content FROM odysseus_ingestion.artifacts WHERE path='src/main.cpp'") == "// apostrophe ' and Unicode café\n", "Artifact update was not rolled back");
        model.artifact.id = "new-rejected-artifact";
        model.artifact.location.path = "new.cpp";
        failed = false;
        try { storage.write(model); } catch (const std::runtime_error&) { failed = true; }
        check(failed, "Expected new model write failure");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.artifacts") == "4", "Orphan artifact survived rollback");
        check(pipeline.ingest(request).artifacts_stored == 4, "Connection did not recover after rollback");
        query(connection.get(), "ALTER TABLE odysseus_ingestion.models DROP CONSTRAINT test_reject");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.models") == "4", "Retry created extra models");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.indexing_tasks") == "8",
              "Expected one task per artifact and target");
        PostgresStorage worker(url); // Separate connection, as a second worker would use.
        auto task = storage.claim_task(IndexTarget::Vector);
        check(task.has_value() && task->attempts == 1, "Cannot claim pending task");
        check(task->payload.contains("content") && task->payload.contains("model"), "Missing retry payload");
        auto other = worker.claim_task(IndexTarget::Vector);
        check(other.has_value() && other->id != task->id, "Workers claimed the same active task");
        check(worker.complete_task(*other), "Cannot complete second task");
        check(storage.renew_task(*task), "Cannot renew active lease");
        check(storage.fail_task(*task, "temporary backend failure", 0), "Cannot fail task");
        check(!storage.complete_task(*task), "Old claim completed failed task");
        // Isolate this task to make the following retry deterministic.
        query(connection.get(), "UPDATE odysseus_ingestion.indexing_tasks SET available_at=now()+interval '1 hour' WHERE status='pending'");
        auto retried = worker.claim_task(IndexTarget::Vector);
        check(retried && retried->id == task->id && retried->attempts == 2 &&
              retried->claim_token != task->claim_token, "Retry did not receive a fresh claim");
        query(connection.get(), "UPDATE odysseus_ingestion.indexing_tasks SET lease_until=now()-interval '1 second' WHERE status='running'");
        check(!worker.complete_task(*retried) && !worker.renew_task(*retried), "Expired lease accepted");
        auto recovered = storage.claim_task(IndexTarget::Vector);
        check(recovered && recovered->id == task->id && recovered->attempts == 3,
              "Crashed worker task was not recovered");
        check(!worker.fail_task(*retried, "stale worker"), "Stale worker changed reclaimed task");
        check(storage.complete_task(*recovered), "Recovered task did not complete");
        check(pipeline.ingest(request).artifacts_stored == 4, "Repeat ingestion failed");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.indexing_tasks WHERE status='completed'") == "2",
              "Unchanged ingestion reset completed tasks");
        // Changing a payload requeues tasks and invalidates any old claim.
        auto fresh_adapter = factory.create(request);
        auto fresh_artifact = fresh_adapter->next();
        ContentClassifier{}.classify(*fresh_artifact);
        auto changed = processor.process(*fresh_artifact);
        query(connection.get(), "UPDATE odysseus_ingestion.indexing_tasks SET available_at=now() WHERE target='graph'");
        auto graph = worker.claim_task(IndexTarget::Graph);
        check(graph && graph->artifact_id == changed.artifact.id, "Unexpected graph task order");
        changed.artifact.content.text += "// updated\n";
        storage.write(changed);
        check(!worker.complete_task(*graph), "Changed payload accepted stale completion");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.indexing_tasks") == "8",
              "Changed payload duplicated tasks");
        // Force enqueue to fail after artifact and model updates: all must roll back.
        query(connection.get(), "ALTER TABLE odysseus_ingestion.indexing_tasks ADD CONSTRAINT reject_queue CHECK (NOT (payload->>'content' = 'reject enqueue'))");
        changed.artifact.content.text = "reject enqueue";
        failed = false;
        try { storage.write(changed); } catch (const std::runtime_error&) { failed = true; }
        check(failed, "Expected enqueue failure");
        check(scalar(connection.get(), "SELECT count(*) FROM odysseus_ingestion.artifacts WHERE content='reject enqueue'") == "0",
              "Artifact committed without its indexing tasks");
        query(connection.get(), "ALTER TABLE odysseus_ingestion.indexing_tasks DROP CONSTRAINT reject_queue");
        std::cout << "PostgreSQL integration passed: 4 artifacts, 4 models, upserts and rollback verified\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
