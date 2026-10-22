#include "graph_export.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>

namespace odysseus::ingestion {
namespace {
std::string string_field(const json& value, const char* key, bool allow_empty = false) {
    const auto result = value.at(key).get<std::string>();
    if (!allow_empty && result.empty()) throw std::runtime_error(std::string("Empty field: ") + key);
    return result;
}
const json& array_field(const json& value, const char* key) {
    const auto& result = value.at(key);
    if (!result.is_array()) throw std::runtime_error(std::string("Expected array: ") + key);
    return result;
}
void source_span(const json& value) {
    const auto& span = value.at("span");
    for (const auto* key : {"start_byte", "end_byte", "start_line", "end_line"}) {
        const auto& number = span.at(key);
        if (!number.is_number_integer() || number < 0)
            throw std::runtime_error(std::string("Invalid source span: ") + key);
    }
    if (span.at("end_byte") < span.at("start_byte") || span.at("start_line") < 1 ||
        span.at("end_line") < span.at("start_line"))
        throw std::runtime_error("Invalid source span bounds");
}
std::string identity(const json& components) { return components.dump(); }
class Builder {
    std::map<std::string, graph::Node> nodes_;
    std::map<std::string, graph::Edge> edges_;
    std::map<std::string, json> files_;
    void node(const std::string& id, const std::string& type, const json& properties) {
        graph::Node entry{id, type, properties};
        auto inserted = nodes_.emplace(id, entry);
        if (!inserted.second && json(inserted.first->second) != json(entry))
            throw std::runtime_error("Conflicting node identity: " + id);
    }
    void edge(const std::string& source, const char* type, const std::string& target) {
        edges_.emplace(identity(json::array({source, type, target})), graph::Edge{source, type, target});
    }
public:
    void add(const json& record) {
        const auto artifact = string_field(record, "artifact_id");
        const auto type = string_field(record, "model_type");
        if (type != "code" && type != "document") throw std::runtime_error("Unsupported model_type: " + type);
        const auto& model = record.at("model");
        const auto& file = model.at("file");
        const auto& provenance = file.at("provenance");
        const auto uri = string_field(provenance, "source_id");
        const auto revision = string_field(provenance, "snapshot_id");
        const auto path = string_field(file, "path");
        std::string extractor;
        if (type == "code") {
            if (!model.at("schema_version").is_number_integer() || model.at("schema_version") != 2)
                throw std::runtime_error("Only code schema_version 2 is supported");
            const auto& parser = model.at("parser");
            for (const auto* key : {"name", "runtime_version", "grammar", "grammar_version", "extractor_version"})
                string_field(parser, key);
            extractor = parser.at("extractor_version");
            const auto status = string_field(model, "parse_status");
            if (status != "complete" && status != "partial") throw std::runtime_error("Invalid parse_status");
        } else {
            if (model.contains("schema_version")) throw std::runtime_error("Versioned document models are unsupported");
            array_field(model, "sections");
        }
        const auto repo_id = identity(json::array({"Repository", uri}));
        const auto rev_id = identity(json::array({"Revision", uri, revision}));
        const auto file_id = identity(json::array({"File", uri, revision, path, type, extractor}));
        auto inserted = files_.emplace(file_id, record);
        if (!inserted.second) {
            if (inserted.first->second != record) throw std::runtime_error("Conflicting file records: " + path);
            return;
        }
        node(repo_id, "Repository", {{"uri", uri}});
        node(rev_id, "Revision", {{"commit", revision}});
        json properties = file;
        properties["artifact_id"] = artifact;
        properties["model_type"] = type;
        if (type == "code") {
            properties["parser"] = model.at("parser");
            properties["parse_status"] = model.at("parse_status");
            properties["diagnostics"] = array_field(model, "diagnostics");
        }
        node(file_id, "File", properties);
        edge(repo_id, "HAS_REVISION", rev_id);
        edge(rev_id, "CONTAINS", file_id);
        if (type == "document") return;
        const auto symbol_id = [&](const char* kind, const std::string& local) {
            return identity(json::array({kind, file_id, local}));
        };
        std::set<std::string> functions;
        for (const auto& fn : array_field(model, "functions")) {
            const auto local = string_field(fn, "id");
            if (!functions.insert(local).second) throw std::runtime_error("Duplicate function id: " + local);
            for (const auto* key : {"name", "qualified_name", "signature", "return_type", "owner_id", "kind"})
                string_field(fn, key, true);
            source_span(fn);
            const auto id = symbol_id("Function", local);
            node(id, "Function", fn);
            edge(file_id, "DEFINES", id);
        }
        for (const auto& cls : array_field(model, "classes")) {
            const auto id = symbol_id("Class", string_field(cls, "id"));
            for (const auto* key : {"name", "qualified_name", "owner_id", "kind"})
                string_field(cls, key, true);
            source_span(cls);
            node(id, "Class", cls);
            edge(file_id, "DEFINES", id);
        }
        for (const auto& call : array_field(model, "calls")) {
            const auto id = symbol_id("CallSite", string_field(call, "id"));
            const auto caller = string_field(call, "caller_id", true);
            source_span(call);
            string_field(call, "owner_id", true);
            string_field(call, "expression", true);
            string_field(call, "callee", true);
            if (string_field(call, "resolution_status") != "unresolved")
                throw std::runtime_error("Only unresolved syntax call sites are supported");
            node(id, "CallSite", call);
            edge(file_id, "CONTAINS", id);
            if (!caller.empty()) {
                if (!functions.count(caller)) throw std::runtime_error("Unknown caller_id: " + caller);
                edge(symbol_id("Function", caller), "HAS_CALL_SITE", id);
            }
        }
    }
    graph::Graph finish() const {
        graph::Graph result;
        for (const auto& entry : nodes_) result.nodes.push_back(entry.second);
        for (const auto& entry : edges_) result.edges.push_back(entry.second);
        return result;
    }
};
}
graph::Graph build_graph(const std::vector<json>& records) {
    Builder builder;
    for (std::size_t i = 0; i < records.size(); ++i) {
        try { builder.add(records[i]); }
        catch (const std::exception& error) {
            throw std::runtime_error("Record " + std::to_string(i + 1) + ": " + error.what());
        }
    }
    return builder.finish();
}
graph::Graph export_graph(const std::filesystem::path& input, const std::filesystem::path& output) {
    if (std::filesystem::exists(output)) throw std::runtime_error("Output already exists: " + output.string());
    if (!std::filesystem::is_directory(input)) throw std::runtime_error("Input is not a directory: " + input.string());
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(input))
        if (entry.is_regular_file() && entry.path().extension() == ".json") paths.push_back(entry.path());
    std::sort(paths.begin(), paths.end());
    Builder builder;
    for (const auto& path : paths) {
        try {
            std::ifstream stream(path);
            if (!stream) throw std::runtime_error("Cannot open input");
            auto record = json::parse(stream);
            builder.add(record);
        } catch (const std::exception& error) {
            throw std::runtime_error(path.string() + ": " + error.what());
        }
    }
    auto result = builder.finish();
    const auto text = json(result).dump(2) + "\n";
    // Exclusive creation also protects against another writer racing the existence check.
    auto* stream = std::fopen(output.c_str(), "wx");
    if (!stream) throw std::runtime_error("Cannot create output (parent must exist): " + output.string());
    const bool written = std::fwrite(text.data(), 1, text.size(), stream) == text.size();
    const bool closed = std::fclose(stream) == 0;
    if (!written || !closed) {
        std::error_code ignored;
        std::filesystem::remove(output, ignored);
        throw std::runtime_error("Failed to write graph: " + output.string());
    }
    return result;
}
}
