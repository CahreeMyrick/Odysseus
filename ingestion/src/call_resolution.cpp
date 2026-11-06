#include "call_resolution.hpp"
#include <filesystem>
#include <map>
#include <regex>
#include <set>

namespace odysseus::ingestion {
namespace {
constexpr const char* version = "odysseus-call-targets-v3";
using Index = std::map<std::string, std::vector<std::string>>;
}

void infer_call_targets(graph::Graph& graph) {
    std::map<std::string, graph::Node*> nodes;
    std::map<std::string, std::string> owners;
    std::map<std::string, Index> symbols;
    Index paths;
    for (auto& node : graph.nodes) nodes[node.id] = &node;
    for (const auto& edge : graph.edges)
        if (nodes.at(edge.source)->type == "File" && nodes.at(edge.target)->type != "File")
            owners[edge.target] = edge.source;
    const auto corpus = [&](const json& file) {
        return json::array({file.at("provenance").at("source_id"),
            file.at("provenance").at("snapshot_id"), file.at("parser").at("grammar"),
            file.at("parser").at("extractor_version")}).dump();
    };
    for (const auto& node : graph.nodes) {
        if (node.type == "File" && node.properties.value("model_type", "") == "code")
            paths[corpus(node.properties) + ":" + node.properties.at("path").get<std::string>()].push_back(node.id);
        if (node.type == "Function" && node.properties.value("kind", "") != "lambda")
            symbols[owners.at(node.id)][node.properties.at("qualified_name")].push_back(node.id);
    }
    // Link sibling header/implementation declarations by exact syntactic
    // signature, retaining both occurrences and their source provenance.
    std::map<std::string, std::string> definitions;
    const std::set<std::string> headers{".h", ".hpp", ".hh", ".hxx"};
    const std::set<std::string> implementations{".c", ".cpp", ".cc", ".cxx"};
    for (const auto& decl : graph.nodes) {
        if (decl.type != "Function" || decl.properties.value("kind", "") != "declaration" ||
            decl.properties.value("signature_key", "").empty()) continue;
        const auto& df = nodes.at(owners.at(decl.id))->properties;
        const auto dp = std::filesystem::path(df.at("path").get<std::string>());
        if (df.at("parse_status") != "complete") continue;
        std::set<std::string> matches;
        for (const auto& fn : graph.nodes) {
            if (fn.type != "Function" || fn.properties.value("kind", "") != "function" ||
                fn.properties.at("qualified_name") != decl.properties.at("qualified_name") ||
                fn.properties.value("signature_key", "") != decl.properties.at("signature_key")) continue;
            const auto& ff = nodes.at(owners.at(fn.id))->properties;
            const auto fp = std::filesystem::path(ff.at("path").get<std::string>());
            if (corpus(df) == corpus(ff) && ff.at("parse_status") == "complete" &&
                (owners.at(decl.id) == owners.at(fn.id) ||
                 (headers.count(dp.extension().string()) && dp.parent_path() == fp.parent_path() &&
                  dp.stem() == fp.stem() && implementations.count(fp.extension().string())))) matches.insert(fn.id);
        }
        if (matches.size() == 1) {
            definitions[decl.id] = *matches.begin();
            graph.edges.push_back({decl.id, "DECLARATION_OF", *matches.begin(),
                {{"evidence_kind", "EXTRACTED"}, {"method", "exact_sibling_signature"},
                 {"resolver_version", version}, {"evidence", json::array({decl.id, *matches.begin()})},
                 {"limitations", "Syntactic signature and sibling-path association, not compiler identity"}}});
        }
    }
    // Includes are retained as syntax. Relative-path reachability is only a
    // candidate heuristic: search paths, preprocessing and linkage are unknown.
    std::map<std::string, std::set<std::string>> includes;
    for (const auto& node : graph.nodes) {
        if (node.type != "File" || !node.properties.contains("parser")) continue;
        for (const auto& inc : node.properties.at("includes")) {
            const auto raw = inc.at("target").get<std::string>();
            const auto kind = inc.value("kind", "unknown");
            if (raw.empty() || std::filesystem::path(raw).is_absolute() ||
                kind == "system" || kind == "macro") continue;
            const auto path = (std::filesystem::path(node.properties.at("path").get<std::string>()).parent_path() / raw).lexically_normal().generic_string();
            const auto found = paths.find(corpus(node.properties) + ":" + path);
            std::string target;
            const char* method = "relative_include";
            if (found != paths.end() && found->second.size() == 1) target = found->second.front();
            if (target.empty() && kind == "quoted" && raw.rfind("./", 0) != 0 && raw.rfind("../", 0) != 0) {
                // Full path suffix matching, never a basename-only fallback for
                // path-qualified includes. Directory proximity breaks only a strict tie.
                std::size_t best = 0;
                std::vector<std::string> matches;
                const auto caller_dir = std::filesystem::path(node.properties.at("path").get<std::string>()).parent_path();
                for (const auto& candidate : graph.nodes) {
                    if (candidate.type != "File" || !candidate.properties.contains("parser") ||
                        corpus(candidate.properties) != corpus(node.properties)) continue;
                    const auto cp = candidate.properties.at("path").get<std::string>();
                    if (!headers.count(std::filesystem::path(cp).extension().string()) ||
                        !(cp == raw || (cp.size() > raw.size() &&
                          cp.compare(cp.size() - raw.size() - 1, raw.size() + 1, "/" + raw) == 0))) continue;
                    const auto directory = std::filesystem::path(cp).parent_path();
                    auto a = caller_dir.begin(), b = directory.begin();
                    std::size_t score = 0;
                    while (a != caller_dir.end() && b != directory.end() && *a == *b) { ++score; ++a; ++b; }
                    if (matches.empty() || score > best) { matches.clear(); best = score; }
                    if (score == best) matches.push_back(candidate.id);
                }
                if (matches.size() == 1) target = matches.front();
                method = "unique_include_suffix";
            }
            if (!target.empty() && includes[node.id].insert(target).second) {
                const bool explicit_path = kind == "quoted" && std::string(method) == "relative_include";
                graph.edges.push_back({node.id, "INCLUDES", target,
                    {{"evidence_kind", explicit_path ? "EXTRACTED" : "INFERRED"}, {"method", method},
                     {"raw_include", raw}, {"include_kind", kind}, {"span", inc.at("span")},
                     {"resolver_version", version}, {"evidence", json::array({node.id, target})},
                     {"limitations", "Syntax/path association; build search order and preprocessor conditions are unknown"}}});
            }
        }
    }
    const std::regex direct(R"((::)?[A-Za-z_][A-Za-z_0-9]*(::[A-Za-z_][A-Za-z_0-9]*)*)");
    const std::regex this_member(R"(this\s*->\s*([A-Za-z_][A-Za-z_0-9]*))");
    const std::regex member(R"(([A-Za-z_][A-Za-z_0-9]*)\s*(\.|->)\s*([A-Za-z_][A-Za-z_0-9]*))");
    for (auto& call : graph.nodes) {
        if (call.type != "CallSite") continue;
        const auto& fid = owners.at(call.id);
        const auto& file = nodes.at(fid)->properties;
        auto& result = call.properties["target_resolution"];
        result = {{"status", "unresolved"}, {"resolver_version", version},
                  {"candidate_ids", json::array()}, {"reason", "no_visible_definition"}};
        if (file.at("parse_status") != "complete") {
            result["reason"] = "partial_parse"; continue;
        }
        const auto expression = call.properties.at("expression").get<std::string>();
        std::smatch receiver_match;
        const bool explicit_this = std::regex_match(expression, receiver_match, this_member);
        std::smatch member_match;
        const bool typed_receiver = !explicit_this && std::regex_match(expression, member_match, member);
        if (!explicit_this && !typed_receiver && !std::regex_match(expression, direct)) {
            result["reason"] = "unsupported_expression"; continue;
        }
        std::map<std::string, json> scopes;
        for (const auto& scope : file.at("scopes")) scopes[scope.at("id")] = scope;
        std::vector<std::string> prefixes;
        std::set<std::string> visited;
        std::vector<std::string> scope_chain;
        std::string enclosing_class;
        bool inside_lambda = false;
        auto owner = call.properties.at("owner_id").get<std::string>();
        bool invalid_scope = false;
        while (!owner.empty()) {
            if (!visited.insert(owner).second || !scopes.count(owner)) { invalid_scope = true; break; }
            scope_chain.push_back(owner);
            const auto& scope = scopes.at(owner);
            const auto kind = scope.at("kind").get<std::string>();
            inside_lambda = inside_lambda || kind == "lambda";
            if (enclosing_class.empty() && (kind == "class" || kind == "struct" || kind == "union"))
                enclosing_class = scope.at("qualified_name").get<std::string>();
            if (kind == "namespace" || kind == "class" || kind == "struct" || kind == "union")
                prefixes.push_back(scope.at("qualified_name").get<std::string>() + "::");
            owner = scope.at("owner_id");
        }
        if (invalid_scope) { result["reason"] = "invalid_scope"; continue; }
        prefixes.push_back("");
        scope_chain.push_back("");
        const bool absolute = expression.rfind("::", 0) == 0;
        if (absolute) prefixes = {""};
        auto spelling = absolute ? expression.substr(2) : expression;
        std::set<std::string> visible{fid};
        std::vector<std::string> pending{fid};
        while (!pending.empty()) {
            const auto current = pending.back(); pending.pop_back();
            for (const auto& next : includes[current])
                if (visible.insert(next).second) pending.push_back(next);
        }
        json receiver_binding;
        std::string receiver_class_id;
        if (typed_receiver) {
            if (inside_lambda) { result["reason"] = "unsupported_receiver_capture"; continue; }
            // Nearest binding wins, including unsupported/auto bindings: never
            // fall through a shadowing declaration to an outer typed variable.
            for (const auto& scope : scope_chain) {
                for (const auto& binding : file.value("bindings", json::array())) {
                    if (binding.at("owner_id") == scope && binding.at("name") == member_match[1].str() &&
                        binding.at("span").at("start_byte") < call.properties.at("span").at("start_byte")) {
                        if (receiver_binding.is_null() || binding.at("span").at("start_byte") >
                            receiver_binding.at("span").at("start_byte")) receiver_binding = binding;
                    }
                }
                if (!receiver_binding.is_null()) break;
            }
            if (receiver_binding.is_null()) { result["reason"] = "unknown_receiver_binding"; continue; }
            auto type = receiver_binding.at("type_name").get<std::string>();
            const auto shape = receiver_binding.at("indirection").get<std::string>();
            if (!std::regex_match(type, direct) || type == "auto" || shape == "unsupported" ||
                (member_match[2].str() == "->") != (shape == "pointer")) {
                result["reason"] = "unsupported_receiver_type"; continue;
            }
            std::set<std::string> classes;
            auto type_prefixes = prefixes;
            if (type.rfind("::", 0) == 0) { type = type.substr(2); type_prefixes = {""}; }
            // Resolve the written type in the binding's scope, not a narrower
            // call-site namespace/class. Prefixes outside that scope are safe.
            if (!receiver_binding.at("owner_id").get<std::string>().empty()) {
                const auto binding_owner = receiver_binding.at("owner_id").get<std::string>();
                type_prefixes.clear();
                auto current = binding_owner;
                std::set<std::string> seen;
                while (!current.empty() && scopes.count(current) && seen.insert(current).second) {
                    const auto& scope = scopes.at(current);
                    const auto kind = scope.at("kind").get<std::string>();
                    if (kind == "namespace" || kind == "class" || kind == "struct" || kind == "union")
                        type_prefixes.push_back(scope.at("qualified_name").get<std::string>() + "::");
                    current = scope.at("owner_id");
                }
                type_prefixes.push_back("");
            } else type_prefixes = {""};
            if (receiver_binding.at("type_name").get<std::string>().rfind("::", 0) == 0) type_prefixes = {""};
            for (const auto& prefix : type_prefixes) {
                for (const auto& node : graph.nodes)
                    if (node.type == "Class" && visible.count(owners.at(node.id)) &&
                        node.properties.at("qualified_name") == prefix + type &&
                        nodes.at(owners.at(node.id))->properties.at("parse_status") == "complete") classes.insert(node.id);
                if (!classes.empty()) break;
            }
            if (classes.size() != 1) { result["reason"] = "unknown_or_ambiguous_receiver_class"; continue; }
            receiver_class_id = *classes.begin();
            prefixes = {nodes.at(receiver_class_id)->properties.at("qualified_name").get<std::string>() + "::"};
            spelling = member_match[3].str();
        }
        if (explicit_this) {
            // A lambda's receiver/capture semantics need a separate pass. Do not
            // mistake lexical containment for its implicit object's identity.
            if (inside_lambda) { result["reason"] = "unsupported_this_context"; continue; }
            if (enclosing_class.empty()) {
                // Out-of-line A::f() has a namespace/file lexical owner. Use the
                // caller's explicit qualifier only when a class of that name exists.
                for (const auto& node : graph.nodes) {
                    if (node.type != "Function" || owners.at(node.id) != fid ||
                        node.properties.at("id") != call.properties.at("caller_id")) continue;
                    const auto qname = node.properties.at("qualified_name").get<std::string>();
                    const auto split = qname.rfind("::");
                    if (split != std::string::npos) enclosing_class = qname.substr(0, split);
                }
            }
            std::size_t classes = 0;
            for (const auto& node : graph.nodes)
                if (node.type == "Class" && visible.count(owners.at(node.id)) &&
                    node.properties.at("qualified_name") == enclosing_class &&
                    nodes.at(owners.at(node.id))->properties.at("parse_status") == "complete") ++classes;
            if (classes != 1) { result["reason"] = "unknown_or_ambiguous_this_class"; continue; }
            prefixes = {enclosing_class + "::"};
            spelling = receiver_match[1].str();
        }
        std::set<std::string> candidates;
        std::set<std::string> declaration_evidence;
        bool unlinked_declaration = false;
        std::string matched;
        for (const auto& prefix : prefixes) {
            matched = prefix + spelling;
            for (const auto& source : visible) {
                if (nodes.at(source)->properties.at("parse_status") != "complete") continue;
                for (const auto& candidate : symbols[source][matched]) {
                    if (definitions.count(candidate)) {
                        candidates.insert(definitions.at(candidate));
                        declaration_evidence.insert(candidate);
                    } else if (nodes.at(candidate)->properties.value("kind", "") == "declaration") {
                        // Declaration-only overloads prevent false uniqueness.
                        unlinked_declaration = true;
                    } else candidates.insert(candidate);
                }
            }
            if (!candidates.empty() || unlinked_declaration) break;
        }
        if (unlinked_declaration) { result["reason"] = "unlinked_declaration"; continue; }
        if (candidates.empty()) continue;
        // EXTRACTED describes the source of the association, not compiler
        // verification. Never promote multiple definitions/overloads.
        const bool extracted = candidates.size() == 1 &&
            !typed_receiver && (explicit_this || expression.find("::") != std::string::npos);
        const auto status = candidates.size() != 1 ? "ambiguous" : extracted ? "extracted" : "inferred";
        const auto method = extracted ? (explicit_this ? "explicit_this_receiver" : "explicit_qualified_name")
                                      : typed_receiver ? "scoped_receiver_type" : "scope_and_relative_include_name_match";
        result["status"] = status;
        result["reason"] = method;
        result["evidence_kind"] = extracted ? "EXTRACTED" : "INFERRED";
        result["candidate_ids"] = candidates;
        for (const auto& target : candidates) {
            auto evidence = json::array({call.id, fid, target, owners.at(target)});
            for (const auto& decl : declaration_evidence) evidence.push_back(decl);
            if (typed_receiver) evidence.push_back(receiver_class_id);
            if (explicit_this)
                for (const auto& node : graph.nodes)
                    if (node.type == "Class" && visible.count(owners.at(node.id)) &&
                        node.properties.at("qualified_name") == enclosing_class) evidence.push_back(node.id);
            json properties = {{"evidence_kind", extracted ? "EXTRACTED" : "INFERRED"}, {"resolution_status", status},
                 {"method", method}, {"resolver_version", version},
                 {"matched_name", matched}, {"evidence", evidence},
                 {"limitations", extracted
                    ? "Explicit syntax association to a unique exported definition; no compiler verification, declaration-only overload checking, preprocessing or runtime dispatch analysis"
                    : "Syntactic candidate only; no overload selection, preprocessing, aliases, inheritance or runtime dispatch analysis"}};
            if (typed_receiver) properties["receiver_binding"] = receiver_binding;
            graph.edges.push_back({call.id, extracted ? "EXTRACTED_TARGET" : "INFERRED_TARGET", target, properties});
        }
    }
}
}
