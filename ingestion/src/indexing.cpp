#include "indexing.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <regex>
#include <cmath>
#include <utility>

namespace odysseus::ingestion {
namespace {
std::string basic(const std::string& input) {
    const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    unsigned value = 0; int bits = -6;
    for (unsigned char c : input) {
        value = (value << 8) | c; bits += 8;
        while (bits >= 0) { output += alphabet[(value >> bits) & 63]; bits -= 6; }
    }
    if (bits > -6) output += alphabet[((value << 8) >> (bits + 8)) & 63];
    while (output.size() % 4) output += '=';
    return output;
}
std::string point_id(const IndexingTask& task, std::size_t chunk) {
    if (task.generation > 0xffffffffLL || chunk > 0xffffffffULL)
        throw IndexingError("Projection identifier limit exceeded");
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << task.id
        << std::setw(8) << task.generation << std::setw(8) << chunk;
    const auto hex = out.str();
    return hex.substr(0,8)+"-"+hex.substr(8,4)+"-"+hex.substr(12,4)+"-"+hex.substr(16,4)+"-"+hex.substr(20);
}
}
IndexingWriter::IndexingWriter(HttpTransport& http, IndexingConfig config)
    : http_(http), config_(std::move(config)) {
    const std::regex identifier("[A-Za-z0-9_-]+");
    if (!std::regex_match(config_.qdrant_collection, identifier) ||
        !std::regex_match(config_.neo4j_database, identifier))
        throw std::invalid_argument("Invalid collection or database name");
    if (config_.qdrant_api_key.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Invalid Qdrant API key");
}
json IndexingWriter::call(const std::string& method, const std::string& url,
                          const json& body, std::vector<std::string> headers) {
    headers.push_back("Content-Type: application/json");
    const auto response = http_.request(method, url, headers, body.dump());
    if (response.status < 200 || response.status >= 300)
        throw IndexingError("Indexing backend HTTP " + std::to_string(response.status));
    const auto result = json::parse(response.body);
    if (result.contains("errors") && !result.at("errors").empty())
        throw IndexingError("Neo4j query failed");
    return result;
}
json IndexingWriter::cypher(const std::string& statement, const json& parameters) {
    std::vector<std::string> headers;
    if (!config_.neo4j_password.empty())
        headers.push_back("Authorization: Basic " + basic(config_.neo4j_user+":"+config_.neo4j_password));
    return call("POST", config_.neo4j_url+"/db/"+config_.neo4j_database+"/query/v2",
                {{"statement", statement}, {"parameters", parameters}}, headers);
}
void IndexingWriter::write(const IndexingTask& task, const std::function<void()>& heartbeat) {
    heartbeat();
    if (task.target == IndexTarget::Vector) vector(task, heartbeat);
    else graph(task);
    heartbeat();
}
void IndexingWriter::vector(const IndexingTask& task, const std::function<void()>& heartbeat) {
    const auto text = task.payload.at("content").get<std::string>();
    if (text.empty()) return; // No semantic content to embed.
    std::vector<std::string> headers;
    if (!config_.qdrant_api_key.empty()) headers.push_back("api-key: " + config_.qdrant_api_key);
    const auto collection = config_.qdrant_url + "/collections/" + config_.qdrant_collection;
    std::size_t start = 0, chunk = 0;
    bool collection_checked = false;
    while (start < text.size()) {
        auto end = std::min(start + 1200, text.size());
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
        const auto content = text.substr(start, end-start);
        heartbeat();
        const auto response = call("POST", config_.ollama_url+"/api/embed",
            {{"model",config_.embedding_model}, {"input","search_document: " + content}, {"truncate",false}});
        const auto& embeddings = response.at("embeddings");
        if (embeddings.size() != 1 || !embeddings[0].is_array() || embeddings[0].empty())
            throw IndexingError("Invalid embedding response");
        const auto& embedding = embeddings[0];
        for (const auto& number : embedding)
            if (!number.is_number() || !std::isfinite(number.get<double>()))
                throw IndexingError("Invalid embedding value");
        if (!collection_checked) {
            const auto existing = http_.get(collection, headers);
            if (existing.status == 404) {
                const auto created = http_.request("PUT", collection,
                    [&] { auto h=headers; h.push_back("Content-Type: application/json"); return h; }(),
                    json{{"vectors",{{"size",embedding.size()},{"distance","Cosine"}}}}.dump());
                // Another worker may have created the collection concurrently.
                if (created.status != 200 && created.status != 201 && created.status != 409)
                    throw IndexingError("Cannot create Qdrant collection");
            } else if (existing.status != 200) throw IndexingError("Cannot inspect Qdrant collection");
            const auto info = call("GET", collection, json::object(), headers);
            const auto& vectors = info.at("result").at("config").at("params").at("vectors");
            if (vectors.at("size") != embedding.size() || vectors.at("distance") != "Cosine")
                throw IndexingError("Qdrant collection embedding configuration mismatch");
            collection_checked = true;
        }
        heartbeat();
        const auto point = json{{"id",point_id(task,chunk)}, {"vector",embedding}, {"payload",{
            {"artifact_id",task.artifact_id},{"task_id",task.id},{"generation",task.generation},
            {"chunk",chunk},{"byte_start",start},{"byte_end",end},{"text",content},
            {"source_uri",task.payload.at("source_uri")},{"revision",task.payload.at("revision")},
            {"path",task.payload.at("path")},{"embedding_model",config_.embedding_model}}}};
        call("PUT", collection+"/points?wait=true", {{"points",json::array({point})}}, headers);
        start = end; ++chunk;
    }
}
void IndexingWriter::graph(const IndexingTask& task) {
    if (!graph_ready_) {
        cypher("CREATE CONSTRAINT odysseus_repo IF NOT EXISTS FOR (n:OdysseusRepository) REQUIRE n.uri IS UNIQUE", json::object());
        cypher("CREATE CONSTRAINT odysseus_revision IF NOT EXISTS FOR (n:OdysseusRevision) REQUIRE n.id IS UNIQUE", json::object());
        cypher("CREATE CONSTRAINT odysseus_projection IF NOT EXISTS FOR (n:OdysseusFile) REQUIRE n.id IS UNIQUE", json::object());
        cypher("CREATE CONSTRAINT odysseus_class IF NOT EXISTS FOR (n:OdysseusClass) REQUIRE n.id IS UNIQUE", json::object());
        cypher("CREATE CONSTRAINT odysseus_function IF NOT EXISTS FOR (n:OdysseusFunction) REQUIRE n.id IS UNIQUE", json::object());
        graph_ready_ = true;
    }
    const auto& p = task.payload;
    const auto revision_id = json::array({p.at("source_uri"), p.at("revision")}).dump();
    const auto file_id = std::to_string(task.id) + ":" + std::to_string(task.generation);
    cypher(R"cypher(
        MERGE (r:OdysseusRepository {uri:$uri})
        MERGE (v:OdysseusRevision {id:$revision_id}) SET v.commit=$revision
        MERGE (r)-[:HAS_REVISION]->(v)
        MERGE (f:OdysseusFile {id:$id})
        SET f.artifact_id=$artifact, f.generation=$generation, f.path=$path,
            f.model_type=$model_type
        MERGE (v)-[:CONTAINS]->(f)
        RETURN f.id
    )cypher", {{"uri", p.at("source_uri")}, {"revision_id", revision_id}, {"revision", p.at("revision")},
        {"id", file_id},
        {"artifact", task.artifact_id}, {"generation", task.generation},
        {"path", p.at("path")}, {"model_type", p.at("model_type")}});

    if (p.at("model_type") == "code" && p.contains("model")) {
        const auto& m = p.at("model");
        if (m.contains("classes") && m.at("classes").is_array()) {
            for (const auto& cls : m.at("classes")) {
                const auto name = cls.at("name").get<std::string>();
                cypher(R"cypher(
                    MERGE (c:OdysseusClass {id:$cid})
                    SET c.name=$name, c.start_line=$start, c.end_line=$end
                    WITH c
                    MATCH (f:OdysseusFile {id:$fid})
                    MERGE (f)-[:DEFINES]->(c)
                )cypher", {{"cid", file_id + "::" + name}, {"name", name},
                           {"start", cls.at("start_line")}, {"end", cls.at("end_line")},
                           {"fid", file_id}});
            }
        }
        if (m.contains("functions") && m.at("functions").is_array()) {
            for (const auto& fn : m.at("functions")) {
                const auto name = fn.at("name").get<std::string>();
                const auto qname = fn.at("qualified_name").get<std::string>();
                cypher(R"cypher(
                    MERGE (fn:OdysseusFunction {id:$fnid})
                    SET fn.name=$name, fn.qualified_name=$qname, fn.signature=$sig,
                        fn.return_type=$ret, fn.start_line=$start, fn.end_line=$end
                    WITH fn
                    MATCH (f:OdysseusFile {id:$fid})
                    MERGE (f)-[:DEFINES]->(fn)
                )cypher", {{"fnid", file_id + "::" + qname}, {"name", name}, {"qname", qname},
                           {"sig", fn.at("signature")}, {"ret", fn.at("return_type")},
                           {"start", fn.at("start_line")}, {"end", fn.at("end_line")},
                           {"fid", file_id}});
            }
        }
        if (m.contains("calls") && m.at("calls").is_array()) {
            for (const auto& call : m.at("calls")) {
                const auto caller = call.at("caller").get<std::string>();
                const auto callee = call.at("callee").get<std::string>();
                cypher(R"cypher(
                    MATCH (caller:OdysseusFunction {id:$caller_id})
                    MERGE (callee:OdysseusFunction {id:$callee_id})
                    ON CREATE SET callee.name=$callee_name, callee.qualified_name=$callee_name
                    MERGE (caller)-[c:CALLS {line:$line}]->(callee)
                )cypher", {{"caller_id", file_id + "::" + caller},
                           {"callee_id", file_id + "::" + callee},
                           {"callee_name", callee},
                           {"line", call.at("line")}});
            }
        }
    }
}
}
