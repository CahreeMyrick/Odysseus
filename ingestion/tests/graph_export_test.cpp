#include "graph_export.hpp"
#include "code_extractor.hpp"
#include "model_json.hpp"
#include "cli.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
using namespace odysseus;
using namespace odysseus::ingestion;
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class F> void fails(F action, const std::string& message) {
    try { action(); } catch (const std::exception& e) {
        check(std::string(e.what()).find(message) != std::string::npos, e.what()); return;
    }
    throw std::runtime_error("Expected failure: " + message);
}
json fixture(const std::string& path = "src/main.cpp", const std::string& revision = "abc") {
    auto model = CodeExtractor::extract("struct A {}; int f() { return helper(); }");
    model.file.path = path;
    model.file.provenance.source_id = "https://github.com/example/repo";
    model.file.provenance.snapshot_id = revision;
    return {{"artifact_id", json::array({model.file.provenance.source_id, revision, path}).dump()},
            {"model_type", "code"}, {"model", model}};
}
int main() {
    try {
        auto first = fixture();
        auto second = fixture("other.cpp");
        auto third = fixture("src/main.cpp", "def");
        document::FileModel doc;
        doc.file.path = "README.md";
        doc.file.provenance.source_id = "https://github.com/example/repo";
        doc.file.provenance.snapshot_id = "abc";
        json document_record = {{"artifact_id", "document"}, {"model_type", "document"}, {"model", doc}};
        std::vector<json> records{first, second, third, document_record};
        const auto graph = build_graph(records);
        check(graph.nodes.size() == 16 && graph.edges.size() == 18, "Unexpected graph counts");
        std::set<std::string> ids;
        std::size_t repos = 0, functions = 0, sites = 0;
        for (const auto& node : graph.nodes) {
            check(ids.insert(node.id).second, "Duplicate global ID");
            repos += node.type == "Repository";
            functions += node.type == "Function";
            if (node.type == "CallSite") {
                ++sites;
                check(node.properties.at("resolution_status") == "unresolved", "Invented resolution");
            }
        }
        check(repos == 1 && functions == 3 && sites == 3, "Deduplication or symbol identity failed");
        for (const auto& edge : graph.edges) {
            check(ids.count(edge.source) && ids.count(edge.target), "Dangling edge");
            check(edge.type != "CALLS", "Invented callee edge");
        }
        std::reverse(records.begin(), records.end());
        records.push_back(first);
        check(json(build_graph(records)) == json(graph), "Output depends on order or duplicate inputs");
        auto invalid = first;
        invalid["model"]["schema_version"] = 1;
        fails([&] { build_graph({invalid}); }, "schema_version");
        invalid = first;
        invalid["model"]["calls"][0]["caller_id"] = "missing";
        fails([&] { build_graph({invalid}); }, "Unknown caller_id");
        invalid = first;
        invalid["model"]["functions"] = "bad";
        fails([&] { build_graph({invalid}); }, "Expected array");
        invalid = first;
        invalid["model"]["functions"][0]["name"] = "conflict";
        fails([&] { build_graph({first, invalid}); }, "Conflicting file");
        invalid = first;
        invalid["model"]["file"]["provenance"].erase("source_id");
        fails([&] { build_graph({invalid}); }, "source_id");
        invalid = first;
        invalid["model"]["functions"][0]["span"]["end_byte"] = -1;
        fails([&] { build_graph({invalid}); }, "Invalid source span");
        auto broken_model = CodeExtractor::extract("int f( { return;");
        auto partial = first;
        partial["model"] = broken_model;
        partial["model"]["file"] = first["model"]["file"];
        const auto partial_graph = build_graph({partial});
        check(!partial_graph.nodes.empty(), "Partial parse rejected");
        const auto root = std::filesystem::temp_directory_path() /
            ("odysseus-graph-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "input");
        struct Cleanup { std::filesystem::path path; ~Cleanup() {
            std::error_code ec; std::filesystem::remove_all(path, ec);
        }} cleanup{root};
        auto output = root / "graph.json";
        std::ofstream(root / "input" / "1.json") << first;
        const auto exported = export_graph(root / "input", output);
        std::ifstream stream(output);
        check(json::parse(stream) == json(exported), "Export roundtrip failed");
        fails([&] { export_graph(root / "input", output); }, "already exists");
        std::ofstream(root / "input" / "bad.json") << "{broken";
        fails([&] { export_graph(root / "input", root / "failed.json"); }, "bad.json");
        check(!std::filesystem::exists(root / "failed.json"), "Malformed input left output");
        std::filesystem::create_directory(root / "empty");
        check(export_graph(root / "empty", root / "empty.json").nodes.empty(), "Empty input failed");
        char app[] = "odysseus", cmd[] = "graph-export", input[] = "models", flag[] = "--output", dest[] = "graph.json";
        char* args[] = {app, cmd, input, flag, dest};
        const auto options = parse_cli(5, args);
        check(options.graph_export && options.graph_input == "models" && options.graph_output == "graph.json", "CLI failed");
        fails([&] { parse_cli(4, args); }, "Expected: graph-export");
        std::cout << "Graph export checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
