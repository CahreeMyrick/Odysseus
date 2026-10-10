#include "indexing.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
using namespace odysseus::ingestion;
namespace {
std::string env(const char* name, const std::string& fallback = {}) {
    const auto* value = std::getenv(name);
    return value && *value ? value : fallback;
}
}
int main(int argc, char* argv[]) {
    try {
        if (argc != 2 || (std::string(argv[1]) != "vector" && std::string(argv[1]) != "graph" &&
                          std::string(argv[1]) != "all")) {
            std::cerr << "Usage: odysseus-index vector|graph|all\nProcesses up to 100 ready tasks per target, then exits.\n";
            return 2;
        }
        const auto database = env("ODYSSEUS_DATABASE_URL");
        if (database.empty()) throw std::runtime_error("Set ODYSSEUS_DATABASE_URL");
        PostgresStorage storage(database);
        storage.initialize_schema();
        IndexingConfig config;
        config.qdrant_url = env("QDRANT_URL", config.qdrant_url);
        config.qdrant_collection = env("QDRANT_COLLECTION", config.qdrant_collection);
        config.qdrant_api_key = env("QDRANT_API_KEY");
        config.ollama_url = env("OLLAMA_URL", config.ollama_url);
        config.embedding_model = env("ODYSSEUS_EMBEDDING_MODEL", config.embedding_model);
        config.neo4j_url = env("NEO4J_URL", config.neo4j_url);
        config.neo4j_database = env("NEO4J_DATABASE", config.neo4j_database);
        config.neo4j_user = env("NEO4J_USER", config.neo4j_user);
        config.neo4j_password = env("NEO4J_PASSWORD");
        CurlTransport http;
        IndexingWriter writer(http, config);
        int completed = 0, failed = 0;
        for (auto target : {IndexTarget::Vector, IndexTarget::Graph}) {
            const auto name = target == IndexTarget::Vector ? "vector" : "graph";
            if (std::string(argv[1]) != "all" && std::string(argv[1]) != name) continue;
            for (int i = 0; i < 100; ++i) {
                auto task = storage.claim_task(target);
                if (!task) break;
                try {
                    writer.write(*task, [&] {
                        if (!storage.renew_task(*task)) throw std::runtime_error("Indexing lease lost");
                    });
                    if (!storage.complete_task(*task)) throw std::runtime_error("Indexing lease lost");
                    ++completed;
                } catch (const std::exception& error) {
                    // Do not persist arbitrary backend diagnostics or source data.
                    const auto* safe = dynamic_cast<const IndexingError*>(&error);
                    const std::string message = safe ? safe->what() :
                        "Backend indexing failed; check service configuration and availability";
                    const bool recorded = storage.fail_task(*task, message);
                    std::cerr << name << " task " << task->id << ": " << message
                              << (recorded ? "; retry available in 60 seconds.\n" : "; claim no longer active.\n");
                    ++failed;
                    break; // Avoid hammering an unavailable backend; still process the other target.
                }
            }
        }
        std::cout << "Completed " << completed << " indexing tasks; failed " << failed << ".\n";
        return failed ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "Indexing failed: " << error.what() << '\n'; return 1;
    }
}
