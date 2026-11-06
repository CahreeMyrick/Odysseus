#include "code_extractor.hpp"
#include "tree_sitter_versions.hpp"

#include <tree_sitter/api.h>
#include <cstring>
#include <cctype>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

extern "C" const TSLanguage* tree_sitter_c();
extern "C" const TSLanguage* tree_sitter_cpp();

namespace odysseus::ingestion {
namespace {
bool is(TSNode node, const char* type) {
    return !ts_node_is_null(node) && std::strcmp(ts_node_type(node), type) == 0;
}
TSNode field(TSNode node, const char* name) {
    if (ts_node_is_null(node)) return {};
    return ts_node_child_by_field_name(node, name, static_cast<uint32_t>(std::strlen(name)));
}
std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    return value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
}
code::SourceSpan span(TSNode node) {
    const auto start = ts_node_start_point(node);
    const auto end = ts_node_end_point(node);
    const auto first = ts_node_start_byte(node), last = ts_node_end_byte(node);
    // A node may include a trailing newline, whose end is column 0 of the next line.
    const auto last_line = end.row + 1 - (last > first && end.column == 0 ? 1 : 0);
    return {first, last, start.row + 1, last_line};
}
std::string occurrence(const char* kind, TSNode node) {
    return std::string(kind) + ":" + std::to_string(ts_node_start_byte(node)) + ":" +
           std::to_string(ts_node_end_byte(node));
}
std::string qualify(const std::string& scope, const std::string& name) {
    if (name.rfind("::", 0) == 0) return name.substr(2);
    if (scope.empty() || name.rfind(scope + "::", 0) == 0) return name;
    return scope + "::" + name;
}
// Follow only declarator links, never parameter types or nested expressions.
TSNode function_declarator(TSNode node) {
    TSNode result{};
    while (!ts_node_is_null(node)) {
        if (is(node, "function_declarator")) result = node;
        auto next = field(node, "declarator");
        if (ts_node_is_null(next) && is(node, "parenthesized_declarator"))
            next = ts_node_named_child(node, 0);
        node = next;
    }
    return result;
}
TSNode terminal_name(TSNode node) {
    while (!ts_node_is_null(node)) {
        TSNode next{};
        if (is(node, "qualified_identifier") || is(node, "template_function") ||
            is(node, "template_method")) next = field(node, "name");
        else if (is(node, "field_expression")) next = field(node, "field");
        if (ts_node_is_null(next)) break;
        node = next;
    }
    return node;
}
TSNode binding_name(TSNode node) {
    while (!ts_node_is_null(node)) {
        if (is(node, "identifier") || is(node, "field_identifier")) return node;
        auto next = field(node, "declarator");
        if (ts_node_is_null(next) && (is(node, "reference_declarator") ||
            is(node, "parenthesized_declarator"))) next = ts_node_named_child(node, 0);
        node = next;
    }
    return {};
}
std::string compact(std::string value) {
    std::string result;
    bool separated = false;
    for (unsigned char c : value) {
        if (std::isspace(c)) { separated = true; continue; }
        if (separated && !result.empty() && (std::isalnum(c) || c == '_') &&
            (std::isalnum(static_cast<unsigned char>(result.back())) || result.back() == '_')) result += ' ';
        result += static_cast<char>(c);
        separated = false;
    }
    return result;
}
struct Context {
    std::string scope_id;
    std::string qualified_scope;
    std::string caller_id;
    std::string caller;
};
struct Pending { TSNode node; Context context; };

class Extractor {
public:
    explicit Extractor(const std::string& source) : source_(source) {}

    // Traverse the parsed tree to populate the code::FileModel.
    code::FileModel run(TSNode root, CodeLanguage language) {
        model_.schema_version = 2;
        model_.parser = {"tree-sitter", ODYSSEUS_TS_VERSION, language == CodeLanguage::C ? "c" : "cpp", language == CodeLanguage::C ? ODYSSEUS_TS_C_VERSION : ODYSSEUS_TS_CPP_VERSION, "odysseus-code-v3"};
        model_.parse_status = ts_node_has_error(root) ? "partial" : "complete";
        std::vector<Pending> pending{{root, {}}};

        // Iterative traversal avoids a C++ stack overflow on deeply nested input.
        while (!pending.empty()) {
            auto current = std::move(pending.back());
            pending.pop_back();
            auto node = current.node;
            auto context = current.context;
            if (is(node, "for_statement") || is(node, "for_range_loop") ||
                is(node, "if_statement") || is(node, "switch_statement") || is(node, "catch_clause")) {
                const auto id = occurrence("control", node);
                model_.scopes.push_back({id, "block", "", context.qualified_scope, context.scope_id, span(node)});
                context.scope_id = id;
                if (is(node, "for_range_loop")) add_binding(node, field(node, "declarator"), context);
                if (is(node, "catch_clause")) {
                    const auto params = field(node, "parameters");
                    for (uint32_t i = 0; i < ts_node_named_child_count(params); ++i) {
                        const auto param = ts_node_named_child(params, i);
                        add_binding(param, field(param, "declarator"), context);
                    }
                }
            }
            if (ts_node_is_error(node) || ts_node_is_missing(node)) {
                model_.diagnostics.push_back({ts_node_is_missing(node) ? "missing" : "error",
                    ts_node_is_missing(node) ? std::string("Missing syntax: ") + ts_node_type(node)
                                             : "Unrecognized syntax", span(node)});
            }

            if (is(node, "preproc_include")) {
                auto path = text(field(node, "path"));
                const auto kind = !path.empty() && path.front() == '"' ? "quoted" :
                                  !path.empty() && path.front() == '<' ? "system" : "macro";
                if (path.size() >= 2 && ((path.front() == '<' && path.back() == '>') ||
                    (path.front() == '"' && path.back() == '"'))) path = path.substr(1, path.size()-2);
                code::Include include;
                include.target = path;
                include.kind = kind;
                include.span = span(node);
                include.line = include.span.start_line;
                model_.includes.push_back(std::move(include));
            } else if (is(node, "namespace_definition")) {
                auto name = text(field(node, "name"));
                if (name.empty()) name = "<anonymous@" + std::to_string(ts_node_start_byte(node)) + ">";
                context = add_scope(node, "namespace", name, context);
            } else if ((is(node, "class_specifier") || is(node, "struct_specifier") ||
                        is(node, "union_specifier")) && !ts_node_is_null(field(node, "body"))) {
                const auto kind = is(node, "class_specifier") ? "class" :
                                  is(node, "struct_specifier") ? "struct" : "union";
                auto name = text(field(node, "name"));
                if (name.empty()) name = "<anonymous@" + std::to_string(ts_node_start_byte(node)) + ">";
                const auto owner = context.scope_id;
                context = add_scope(node, kind, name, context);
                code::ClassDefinition cls;
                cls.name = name; cls.id = context.scope_id; cls.owner_id = owner;
                cls.qualified_name = context.qualified_scope; cls.kind = kind;
                cls.span = span(node); cls.start_line = cls.span.start_line; cls.end_line = cls.span.end_line;
                model_.classes.push_back(std::move(cls));
                context.caller.clear(); context.caller_id.clear();
            } else if (is(node, "function_definition") && !ts_node_is_null(field(node, "body"))) {
                const auto declarator = function_declarator(field(node, "declarator"));
                const auto name_node = ts_node_is_null(declarator) ? TSNode{} : field(declarator, "declarator");
                if (ts_node_is_null(name_node)) {
                    model_.parse_status = "partial";
                    model_.diagnostics.push_back({"unsupported", "Unsupported function declarator", span(node)});
                    context.caller.clear(); context.caller_id.clear();
                } else {
                    const auto spelling = text(name_node);
                    const auto name = text(terminal_name(name_node));
                    code::FunctionDefinition fn;
                    fn.id = occurrence("function", node); fn.owner_id = context.scope_id;
                    fn.name = name; fn.qualified_name = qualify(context.qualified_scope, spelling);
                    fn.span = span(node); fn.start_line = fn.span.start_line; fn.end_line = fn.span.end_line;
                    fn.signature = trim(source_.substr(ts_node_start_byte(node),
                        ts_node_start_byte(field(node, "body")) - ts_node_start_byte(node)));
                    // This is the declared source spelling, not a resolved C++ type.
                    fn.return_type = trim(source_.substr(ts_node_start_byte(node),
                        ts_node_start_byte(name_node) - ts_node_start_byte(node)));
                    fn.signature_key = signature_key(declarator);
                    model_.functions.push_back(fn);
                    model_.scopes.push_back({fn.id, "function", fn.name, fn.qualified_name, fn.owner_id, fn.span});
                    context = {fn.id, fn.qualified_name, fn.id, fn.qualified_name};
                    const auto params = field(declarator, "parameters");
                    for (uint32_t i = 0; i < ts_node_named_child_count(params); ++i) {
                        const auto param = ts_node_named_child(params, i);
                        add_binding(param, field(param, "declarator"), context);
                    }
                }
            } else if (is(node, "declaration") || is(node, "field_declaration")) {
                for (uint32_t i = 0; i < ts_node_child_count(node); ++i) {
                    const auto name = ts_node_field_name_for_child(node, i);
                    if (!name || std::strcmp(name, "declarator")) continue;
                    const auto decl = ts_node_child(node, i);
                    const auto func = function_declarator(decl);
                    const auto target = field(func, "declarator");
                    if (!ts_node_is_null(func) && (is(target, "identifier") ||
                        is(target, "field_identifier") || is(target, "qualified_identifier"))) {
                        code::FunctionDefinition fn;
                        fn.id = occurrence("declaration", decl); fn.owner_id = context.scope_id;
                        fn.kind = "declaration"; fn.name = text(terminal_name(target));
                        fn.qualified_name = qualify(context.qualified_scope, text(target));
                        fn.signature = text(node); fn.return_type = text(field(node, "type"));
                        fn.span = span(node); fn.start_line = fn.span.start_line; fn.end_line = fn.span.end_line;
                        fn.signature_key = signature_key(func);
                        model_.declarations.push_back(std::move(fn));
                    } else add_binding(node, decl, context);
                }
            } else if (is(node, "lambda_expression")) {
                code::FunctionDefinition fn;
                fn.id = occurrence("lambda", node); fn.owner_id = context.scope_id; fn.kind = "lambda";
                fn.name = "<lambda@" + std::to_string(ts_node_start_byte(node)) + ">";
                fn.qualified_name = qualify(context.qualified_scope, fn.name);
                fn.span = span(node); fn.start_line = fn.span.start_line; fn.end_line = fn.span.end_line;
                fn.signature = trim(source_.substr(ts_node_start_byte(node),
                    ts_node_start_byte(field(node, "body")) - ts_node_start_byte(node)));
                model_.functions.push_back(fn);
                model_.scopes.push_back({fn.id, "lambda", fn.name, fn.qualified_name, fn.owner_id, fn.span});
                context = {fn.id, fn.qualified_name, fn.id, fn.qualified_name};
            } else if (is(node, "compound_statement")) {
                // Preserve block ownership for a future scope-aware resolver.
                const auto id = occurrence("block", node);
                model_.scopes.push_back({id, "block", "", context.qualified_scope, context.scope_id, span(node)});
                context.scope_id = id;
            } else if (is(node, "call_expression")) {
                const auto target = field(node, "function");
                code::FunctionCall call;
                call.id = occurrence("call", node); call.caller = context.caller; call.caller_id = context.caller_id;
                call.owner_id = context.scope_id;
                call.callee = text(terminal_name(target));
                call.expression = text(target);
                call.span = span(node); call.line = call.span.start_line;
                // An observed call expression is not evidence of a resolved target.
                model_.calls.push_back(std::move(call));
            }
            // Include unnamed children to capture missing punctuation diagnostics.
            for (uint32_t i = ts_node_child_count(node); i > 0; --i) {
                const auto child = ts_node_child(node, i-1);
                auto child_context = context;
                // Lambda captures execute in the enclosing context, not the body.
                if (is(node, "lambda_expression") && !ts_node_eq(child, field(node, "body")))
                    child_context = current.context;
                pending.push_back({child, std::move(child_context)});
            }
        }
        return std::move(model_);
    }
private:
    std::string signature_key(TSNode declarator) const {
        if (ts_node_is_null(declarator)) return {};
        const auto params = field(declarator, "parameters");
        if (ts_node_is_null(params)) return {};
        std::string key = "(";
        for (uint32_t i = 0; i < ts_node_named_child_count(params); ++i) {
            const auto param = ts_node_named_child(params, i);
            if (!is(param, "parameter_declaration") && !is(param, "optional_parameter_declaration")) return {};
            const auto type = field(param, "type");
            const auto decl = field(param, "declarator");
            if (ts_node_is_null(type) || !ts_node_is_null(function_declarator(decl))) return {};
            const auto end = ts_node_is_null(decl) ? ts_node_end_byte(type) : ts_node_end_byte(decl);
            auto spelling = source_.substr(ts_node_start_byte(param), end - ts_node_start_byte(param));
            const auto name = binding_name(decl);
            if (!ts_node_is_null(name)) spelling.erase(ts_node_start_byte(name) - ts_node_start_byte(param),
                                                      ts_node_end_byte(name) - ts_node_start_byte(name));
            // Arrays and complex declarators require language type adjustment.
            if (spelling.find('[') != std::string::npos || spelling.find('(') != std::string::npos) return {};
            if (i) key += ",";
            key += compact(spelling);
        }
        key += ")";
        for (uint32_t i = 0; i < ts_node_named_child_count(declarator); ++i) {
            const auto child = ts_node_named_child(declarator, i);
            if (is(child, "type_qualifier") || is(child, "ref_qualifier")) key += compact(text(child));
        }
        return key;
    }
    void add_binding(TSNode node, TSNode decl, const Context& context) {
        const auto name = binding_name(decl);
        if (ts_node_is_null(name)) return;
        auto shape = text(decl);
        const auto equal = shape.find('=');
        if (equal != std::string::npos) shape.resize(equal);
        std::string indirection = "value";
        if (!ts_node_is_null(function_declarator(decl)) || shape.find('[') != std::string::npos)
            indirection = "unsupported";
        else if (shape.find('*') != std::string::npos)
            indirection = shape.find('*', shape.find('*') + 1) == std::string::npos ? "pointer" : "unsupported";
        else if (shape.find('&') != std::string::npos) indirection = "reference";
        model_.bindings.push_back({occurrence("binding", name), text(name), text(field(node, "type")),
                                   context.scope_id, indirection, span(name)});
    }
    std::string text(TSNode node) const {
        if (ts_node_is_null(node)) return {};
        return source_.substr(ts_node_start_byte(node), ts_node_end_byte(node) - ts_node_start_byte(node));
    }
    Context add_scope(TSNode node, const char* kind, const std::string& name, Context context) {
        const auto id = occurrence(kind, node);
        const auto qualified = qualify(context.qualified_scope, name);
        model_.scopes.push_back({id, kind, name, qualified, context.scope_id, span(node)});
        context.scope_id = id; context.qualified_scope = qualified;
        return context;
    }
    const std::string& source_;
    code::FileModel model_;
};
} // namespace

code::FileModel CodeExtractor::extract(const std::string& source_text, CodeLanguage language) {
    if (source_text.size() > std::numeric_limits<uint32_t>::max()) {
        throw std::length_error("Source exceeds Tree-sitter's byte-offset limit");
    }

    // instantiate parser
    std::unique_ptr<TSParser, decltype(&ts_parser_delete)> parser(ts_parser_new(), ts_parser_delete);

    if (!parser) {
        throw std::runtime_error("Cannot allocate Tree-sitter parser");
    }

    // specific language
    const auto grammar = language == CodeLanguage::C ? tree_sitter_c() : tree_sitter_cpp();

    if (!ts_parser_set_language(parser.get(), grammar)) {
        throw std::runtime_error("Incompatible Tree-sitter grammar ABI");
    }

    // execute parsing
    // ts_parser_parse_string peforms the parsing, it returns a TSTree* which we wrap in a unique_ptr for automatic cleanup
    std::unique_ptr<TSTree, decltype(&ts_tree_delete)> tree(ts_parser_parse_string(parser.get(), nullptr, source_text.data(), static_cast<uint32_t>(source_text.size())), ts_tree_delete);

    if (!tree) {
        throw std::runtime_error("Tree-sitter could not parse source");
    }
    return Extractor(source_text).run(ts_tree_root_node(tree.get()), language);
}
} // namespace odysseus::ingestion
