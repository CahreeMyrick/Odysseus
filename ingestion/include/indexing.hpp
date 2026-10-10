#pragma once
#include "postgres_storage.hpp"
#include "http_transport.hpp"
#include <functional>
namespace odysseus::ingestion {
class IndexingError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct IndexingConfig {
    std::string qdrant_url = "http://localhost:16333";
    std::string qdrant_collection = "odysseus_nomic_v1";
    std::string qdrant_api_key;
    std::string ollama_url = "http://localhost:11435";
    std::string embedding_model = "nomic-embed-text";
    std::string neo4j_url = "http://localhost:17474";
    std::string neo4j_database = "neo4j";
    std::string neo4j_user = "neo4j";
    std::string neo4j_password;
};
// Projections are versioned by task ID and generation; retries overwrite the same keys.
class IndexingWriter {
public:
    IndexingWriter(HttpTransport& http, IndexingConfig config);
    void write(const IndexingTask& task, const std::function<void()>& heartbeat);
private:
    json call(const std::string& method, const std::string& url, const json& body,
              std::vector<std::string> headers = {});
    json cypher(const std::string& statement, const json& parameters);
    void vector(const IndexingTask& task, const std::function<void()>& heartbeat);
    void graph(const IndexingTask& task);
    HttpTransport& http_;
    IndexingConfig config_;
    bool graph_ready_ = false;
};
}
