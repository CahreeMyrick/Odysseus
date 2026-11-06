#include "code_extractor.hpp"
#include "graph_export.hpp"
#include "model_json.hpp"
#include <iostream>
using namespace odysseus;
using namespace odysseus::ingestion;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
json record(const std::string& path, const std::string& source, const std::string& revision = "test") {
    auto model = CodeExtractor::extract(source);
    check(model.parse_status == "complete", "Fixture failed to parse");
    model.file.path = path;
    model.file.provenance.source_id = "test://repo";
    model.file.provenance.snapshot_id = revision;
    return {{"artifact_id", path}, {"model_type", "code"}, {"model", model}};
}
std::vector<graph::Edge> edges(const graph::Graph& graph, const std::string& type) {
    std::vector<graph::Edge> found;
    for (const auto& edge : graph.edges) if (edge.type == type) found.push_back(edge);
    return found;
}
int main() {
    try {
        const auto header = record("Widget.hpp", "struct Widget { int run(int value) const; }; int free_fn(int arg);");
        const auto impl = record("Widget.cpp", "#include \"Widget.hpp\"\nint Widget::run(int renamed) const { return renamed; } int free_fn(int other) { return other; }");
        const auto caller = record("main.cpp", "#include \"Widget.hpp\"\nint f(Widget& w, Widget* p) { Widget local; return w.run(1) + p->run(2) + local.run(3) + free_fn(4); }");
        auto graph = build_graph({header, impl, caller});
        check(edges(graph, "DECLARATION_OF").size() == 2, "Header definitions not linked");
        auto inferred = edges(graph, "INFERRED_TARGET");
        check(inferred.size() == 4, "Cross-file typed member/free calls missing");
        int typed = 0;
        for (const auto& edge : inferred) {
            check(edge.target.find("Widget.cpp") != std::string::npos, "Resolved to declaration instead of implementation");
            if (edge.properties.at("method") == "scoped_receiver_type") {
                ++typed;
                check(edge.properties.contains("receiver_binding"), "Receiver provenance missing");
            }
        }
        check(typed == 3, "Missing reference/pointer/local bindings");
        check(json(graph) == json(build_graph({caller, impl, header, header})), "Order-dependent linkage");
        auto old = impl;
        old["model"]["file"]["provenance"]["snapshot_id"] = "old";
        check(edges(build_graph({header, old, caller}), "INFERRED_TARGET").empty(), "Cross-revision linkage");
        auto unrelated = impl;
        unrelated["model"]["file"]["path"] = "other/Widget.cpp";
        check(edges(build_graph({header, unrelated, caller}), "DECLARATION_OF").empty(), "Unrelated implementation merged");
        auto duplicate = impl;
        duplicate["model"]["file"]["path"] = "Widget.cc";
        check(edges(build_graph({header, impl, duplicate, caller}), "DECLARATION_OF").empty(), "Ambiguous implementation selected");
        const auto shadow = record("shadow.cpp",
            "struct A { int run() { return 1; } }; struct B { int run() { return 2; } }; "
            "int f(A a) { a.run(); { B a; a.run(); } { auto a = 0; a.run(); } return a.run(); }");
        auto shadow_graph = build_graph({shadow});
        int a = 0, b = 0, unknown = 0;
        for (const auto& edge : edges(shadow_graph, "INFERRED_TARGET")) {
            a += edge.properties.at("matched_name") == "A::run";
            b += edge.properties.at("matched_name") == "B::run";
        }
        for (const auto& node : shadow_graph.nodes)
            if (node.type == "CallSite") unknown += node.properties.at("target_resolution").at("reason") == "unsupported_receiver_type";
        check(a == 2 && b == 1 && unknown == 1, "Shadowing or scope restoration failed");
        const auto overload_header = record("Widget.hpp", "struct Widget { int run(int); int run(double); };");
        const auto overload_impl = record("Widget.cpp", "int Widget::run(int x) { return x; } int Widget::run(double x) { return 0; }");
        const auto overload_call = record("main.cpp", "#include \"Widget.hpp\"\nint f(Widget w) { return w.run(1); }");
        auto overloads = edges(build_graph({overload_header, overload_impl, overload_call}), "INFERRED_TARGET");
        check(overloads.size() == 2, "Overloads collapsed");
        for (const auto& edge : overloads) check(edge.properties.at("resolution_status") == "ambiguous", "Overload guessed");
        const auto missing_overload = record("Widget.cpp", "int Widget::run(int x) { return x; }");
        check(edges(build_graph({overload_header, missing_overload, overload_call}), "INFERRED_TARGET").empty(), "Declaration-only overload ignored");
        const auto loop = record("loop.cpp",
            "struct A { int run() { return 1; } }; int f(A a) { for (int a = 0; a < 1; ++a) { a.run(); } return a.run(); }");
        check(edges(build_graph({loop}), "INFERRED_TARGET").size() == 1, "Loop binding leaked or fell through");
        const auto after = record("after.cpp",
            "struct A { int run() { return 1; } }; int f() { a.run(); A a; return a.run(); }");
        check(edges(build_graph({after}), "INFERRED_TARGET").size() == 1, "Binding used before declaration");
        const auto suffix_header = record("src/position/Widget.hpp", "struct Widget { int run() { return 1; } };");
        const auto suffix_call = record("scripts/main.cpp", "#include \"position/Widget.hpp\"\nint f(Widget w) { return w.run(); }");
        auto suffix_graph = build_graph({suffix_call, suffix_header});
        check(edges(suffix_graph, "INFERRED_TARGET").size() == 1, "Include suffix did not expose class");
        auto include_edges = edges(suffix_graph, "INCLUDES");
        check(include_edges.size() == 1 && include_edges[0].properties.at("evidence_kind") == "INFERRED",
              "Suffix include association not labeled inferred");
        const auto tied_header = record("vendor/position/Widget.hpp", "struct Widget { int run() { return 2; } };");
        check(edges(build_graph({suffix_call, suffix_header, tied_header}), "INCLUDES").empty(), "Ambiguous include guessed");
        const auto system_call = record("scripts/main.cpp", "#include <position/Widget.hpp>\nint f(Widget w) { return w.run(); }");
        check(edges(build_graph({system_call, suffix_header}), "INCLUDES").empty(), "System include guessed");
        const auto explicit_missing = record("scripts/main.cpp", "#include \"./position/Widget.hpp\"\nint f(Widget w) { return w.run(); }");
        check(edges(build_graph({explicit_missing, suffix_header}), "INCLUDES").empty(), "Explicit missing relative include rescued incorrectly");
        const auto cycle_a = record("a.hpp", "#include \"b.hpp\"\nstruct A { int run() { return 1; } };");
        const auto cycle_b = record("b.hpp", "#include \"a.hpp\"\nint f(A a) { return a.run(); }");
        check(edges(build_graph({cycle_a, cycle_b}), "INFERRED_TARGET").size() == 1, "Include cycle failed");
        std::cout << "Call resolution checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
