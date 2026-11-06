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
json fixture(const std::string& path = "src/main.cpp", const std::string& revision = "abc",
             const std::string& source = "struct A {}; int f() { return helper(); }") {
    auto model = CodeExtractor::extract(source);
    model.file.path = path;
    model.file.provenance.source_id = "https://github.com/example/repo";
    model.file.provenance.snapshot_id = revision;
    return {{"artifact_id", json::array({model.file.provenance.source_id, revision, path}).dump()},
            {"model_type", "code"}, {"model", model}};
}
int main() {
    try {
        auto caller = fixture("src/main.cpp", "abc",
            "#include \"helpers.hpp\"\nnamespace other { int helper() { return 2; } }\n"
            "int f() { return helper() + other::helper() + missing(); }");
        auto header = fixture("src/helpers.hpp", "abc", "int helper() { return 1; }");
        auto unrelated = fixture("elsewhere.cpp", "abc", "int helper() { return 3; }");
        auto historical = fixture("src/helpers.hpp", "old", "int helper() { return 4; }");
        auto inferred = build_graph({caller, header, unrelated, historical});
        check(inferred.schema_version == 4, "Expected graph v4");
        int inferred_count = 0;
        for (const auto& edge : inferred.edges) {
            if (edge.type != "INFERRED_TARGET") {
                check(edge.properties.at("evidence_kind") == "EXTRACTED", "Missing explicit evidence");
                continue;
            }
            ++inferred_count;
            check(edge.properties.at("evidence_kind") == "INFERRED", "Inference presented as fact");
            check(edge.properties.at("resolution_status") == "inferred", "Unexpected ambiguity");
            check(edge.target.find("elsewhere.cpp") == std::string::npos &&
                  edge.target.find("old") == std::string::npos, "Escaped visibility/revision");
        }
        check(inferred_count == 1, "Missing cross-file candidate or qualified call not extracted");
        int explicit_count = 0;
        for (const auto& edge : inferred.edges)
            if (edge.type == "EXTRACTED_TARGET") {
                ++explicit_count;
                check(edge.properties.at("method") == "explicit_qualified_name" &&
                      edge.properties.at("matched_name") == "other::helper", "Wrong explicit target");
            }
        check(explicit_count == 1, "Missing explicit qualified target");
        check(json(inferred) == json(build_graph({historical, unrelated, header, caller, caller})),
              "Inference depends on input order");
        auto overloaded = fixture("overloads.cpp", "abc",
            "int h(int x) { return x; } int h(double x) { return 0; } int f() { return h(1); }");
        int ambiguous = 0;
        for (const auto& edge : build_graph({overloaded}).edges)
            if (edge.type == "INFERRED_TARGET") {
                ++ambiguous;
                check(edge.properties.at("resolution_status") == "ambiguous", "Guessed overload");
            }
        check(ambiguous == 2, "Lost overload candidates");
        auto scoped = fixture("scoped.cpp", "abc",
            "int h() { return 0; } namespace n { int h() { return 1; } int f() { return h(); } }");
        for (const auto& edge : build_graph({scoped}).edges)
            if (edge.type == "INFERRED_TARGET")
                check(edge.properties.at("matched_name") == "n::h", "Ignored namespace scope");
        auto unsupported = fixture("member.cpp", "abc",
            "struct A { int h() { return 1; } }; int f(A a) { return a.h(); }");
        for (const auto& node : build_graph({unsupported}).nodes)
            if (node.type == "CallSite")
                check(node.properties.at("target_resolution").at("status") == "inferred",
                      "Typed parameter receiver not inferred");
        auto partial_candidate = header;
        partial_candidate["model"]["parse_status"] = "partial";
        for (const auto& edge : build_graph({caller, partial_candidate}).edges)
            if (edge.type == "INFERRED_TARGET" || edge.type == "EXTRACTED_TARGET")
                check(edge.properties.at("matched_name") == "other::helper", "Used partial target");
        auto members = fixture("members.cpp", "abc",
            "struct lower { int h() { return 1; } int g(); int f() { return this -> h(); } }; "
            "struct Other { int h() { return 2; } }; "
            "int lower::g() { return this->h() + lower::h(); }");
        int explicit_members = 0;
        for (const auto& edge : build_graph({members}).edges)
            if (edge.type == "EXTRACTED_TARGET") {
                ++explicit_members;
                check(edge.properties.at("matched_name") == "lower::h", "Receiver collision");
                check(edge.properties.at("resolution_status") == "extracted", "Wrong explicit status");
                if (edge.properties.at("method") == "explicit_this_receiver")
                    check(edge.properties.at("evidence").size() == 5, "Missing receiver evidence");
            }
        check(explicit_members == 3, "Missing inline/out-of-line this or lower-case qualified target");
        auto ambiguous_members = fixture("ambiguous.cpp", "abc",
            "struct A { static int h(int x) { return x; } static int h(double x) { return 0; } "
            "int f() { return this->h(1); } }; int f() { return A::h(1); }");
        int ambiguous_members_count = 0;
        for (const auto& edge : build_graph({ambiguous_members}).edges) {
            check(edge.type != "EXTRACTED_TARGET", "Ambiguous overload promoted to explicit");
            if (edge.type == "INFERRED_TARGET") {
                ++ambiguous_members_count;
                check(edge.properties.at("resolution_status") == "ambiguous", "Lost explicit ambiguity");
            }
        }
        check(ambiguous_members_count == 4, "Missing ambiguous qualified/this candidates");
        auto lambda_this = fixture("lambda.cpp", "abc",
            "struct A { int h() { return 1; } int f() { auto l = [this]() { return this->h(); }; return 0; } };");
        for (const auto& node : build_graph({lambda_this}).nodes)
            if (node.type == "CallSite")
                check(node.properties.at("target_resolution").at("reason") == "unsupported_this_context",
                      "Guessed lambda receiver");
        auto global_call = fixture("global.cpp", "abc",
            "int h() { return 1; } namespace n { int h() { return 2; } int f() { return ::h(); } }");
        int global_matches = 0;
        for (const auto& edge : build_graph({global_call}).edges)
            if (edge.type == "EXTRACTED_TARGET") {
                ++global_matches;
                check(edge.properties.at("matched_name") == "h", "Ignored absolute qualification");
            }
        check(global_matches == 1, "Missing absolute qualified target");
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
