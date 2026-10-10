#include "code_extractor.hpp"

#include <cctype>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

namespace odysseus::ingestion {
namespace {

enum class TokenKind {
    Identifier,
    Keyword,
    Punctuation,
    Number,
    String,
    Char,
    Eof
};

struct Token {
    TokenKind kind = TokenKind::Eof;
    std::string text;
    std::size_t line = 1;
    std::size_t col = 1;
};

const std::set<std::string> control_keywords = {
    "if", "while", "for", "switch", "catch", "return", "sizeof", "alignof",
    "decltype", "typeid", "static_cast", "dynamic_cast", "const_cast",
    "reinterpret_cast", "new", "delete", "throw", "defined", "case", "default"
};

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

std::string trim(const std::string& s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Tokenize and extract preprocessor includes
std::vector<Token> tokenize(const std::string& src, std::vector<code::Include>& includes) {
    std::vector<Token> tokens;
    const std::size_t len = src.size();
    std::size_t i = 0;
    std::size_t line = 1;
    std::size_t col = 1;

    while (i < len) {
        char c = src[i];

        // Newline
        if (c == '\n') {
            ++line;
            col = 1;
            ++i;
            continue;
        }

        // Whitespace
        if (std::isspace(static_cast<unsigned char>(c))) {
            ++col;
            ++i;
            continue;
        }

        // Single-line comment
        if (c == '/' && i + 1 < len && src[i + 1] == '/') {
            i += 2;
            col += 2;
            while (i < len && src[i] != '\n') {
                ++i;
                ++col;
            }
            continue;
        }

        // Multi-line comment
        if (c == '/' && i + 1 < len && src[i + 1] == '*') {
            i += 2;
            col += 2;
            while (i + 1 < len && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') {
                    ++line;
                    col = 1;
                } else {
                    ++col;
                }
                ++i;
            }
            if (i + 1 < len) {
                i += 2;
                col += 2;
            }
            continue;
        }

        // Preprocessor directive
        if (c == '#') {
            const std::size_t pp_line = line;
            ++i;
            ++col;
            // Read directive name
            while (i < len && (src[i] == ' ' || src[i] == '\t')) {
                ++i;
                ++col;
            }
            std::string directive;
            while (i < len && is_identifier_char(src[i])) {
                directive += src[i];
                ++i;
                ++col;
            }
            if (directive == "include") {
                while (i < len && (src[i] == ' ' || src[i] == '\t')) {
                    ++i;
                    ++col;
                }
                if (i < len && (src[i] == '<' || src[i] == '"')) {
                    char term = src[i] == '<' ? '>' : '"';
                    ++i;
                    ++col;
                    std::string target;
                    while (i < len && src[i] != term && src[i] != '\n') {
                        target += src[i];
                        ++i;
                        ++col;
                    }
                    if (i < len && src[i] == term) {
                        ++i;
                        ++col;
                    }
                    includes.push_back({target, pp_line});
                }
            }
            // Skip the rest of the preprocessor line (including backslash continuations)
            while (i < len && src[i] != '\n') {
                if (src[i] == '\\' && i + 1 < len && src[i + 1] == '\n') {
                    ++line;
                    col = 1;
                    i += 2;
                    continue;
                }
                ++i;
                ++col;
            }
            continue;
        }

        // Raw string literal R"delim(...)delim"
        if (c == 'R' && i + 1 < len && src[i + 1] == '"') {
            const std::size_t tok_line = line;
            const std::size_t tok_col = col;
            i += 2;
            col += 2;
            std::string delim;
            while (i < len && src[i] != '(' && src[i] != '\n') {
                delim += src[i];
                ++i;
                ++col;
            }
            if (i < len && src[i] == '(') {
                ++i;
                ++col;
            }
            const std::string end_seq = ")" + delim + "\"";
            std::size_t match_pos = src.find(end_seq, i);
            if (match_pos != std::string::npos) {
                while (i < match_pos + end_seq.size()) {
                    if (src[i] == '\n') {
                        ++line;
                        col = 1;
                    } else {
                        ++col;
                    }
                    ++i;
                }
            } else {
                i = len;
            }
            tokens.push_back({TokenKind::String, "", tok_line, tok_col});
            continue;
        }

        // Standard string literal
        if (c == '"') {
            const std::size_t tok_line = line;
            const std::size_t tok_col = col;
            ++i;
            ++col;
            while (i < len && src[i] != '"') {
                if (src[i] == '\\' && i + 1 < len) {
                    if (src[i + 1] == '\n') {
                        ++line;
                        col = 1;
                    } else {
                        col += 2;
                    }
                    i += 2;
                } else if (src[i] == '\n') {
                    ++line;
                    col = 1;
                    ++i;
                } else {
                    ++col;
                    ++i;
                }
            }
            if (i < len && src[i] == '"') {
                ++i;
                ++col;
            }
            tokens.push_back({TokenKind::String, "", tok_line, tok_col});
            continue;
        }

        // Character literal
        if (c == '\'') {
            const std::size_t tok_line = line;
            const std::size_t tok_col = col;
            ++i;
            ++col;
            while (i < len && src[i] != '\'') {
                if (src[i] == '\\' && i + 1 < len) {
                    col += 2;
                    i += 2;
                } else {
                    ++col;
                    ++i;
                }
            }
            if (i < len && src[i] == '\'') {
                ++i;
                ++col;
            }
            tokens.push_back({TokenKind::Char, "", tok_line, tok_col});
            continue;
        }

        // Identifier or keyword
        if (is_identifier_start(c)) {
            const std::size_t tok_line = line;
            const std::size_t tok_col = col;
            std::string id;
            while (i < len && is_identifier_char(src[i])) {
                id += src[i];
                ++i;
                ++col;
            }
            tokens.push_back({TokenKind::Identifier, id, tok_line, tok_col});
            continue;
        }

        // Number literal
        if (std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t tok_line = line;
            const std::size_t tok_col = col;
            std::string num;
            while (i < len && (is_identifier_char(src[i]) || src[i] == '.')) {
                num += src[i];
                ++i;
                ++col;
            }
            tokens.push_back({TokenKind::Number, num, tok_line, tok_col});
            continue;
        }

        // Two-character punctuation
        if (i + 1 < len) {
            std::string two = src.substr(i, 2);
            if (two == "::" || two == "->" || two == "==" || two == "!=" ||
                two == "<=" || two == ">=" || two == "&&" || two == "||" ||
                two == "++" || two == "--" || two == "<<" || two == ">>") {
                tokens.push_back({TokenKind::Punctuation, two, line, col});
                i += 2;
                col += 2;
                continue;
            }
        }

        // Single-character punctuation
        tokens.push_back({TokenKind::Punctuation, std::string(1, c), line, col});
        ++i;
        ++col;
    }

    return tokens;
}

} // namespace

code::FileModel CodeExtractor::extract(const std::string& source_text) {
    code::FileModel model;
    std::vector<Token> tokens = tokenize(source_text, model.includes);
    const std::size_t num_tokens = tokens.size();

    std::vector<std::string> class_stack;
    struct ClassScope {
        std::string name;
        std::size_t start_line;
        int brace_depth;
    };
    std::vector<ClassScope> active_classes;

    struct FunctionScope {
        std::string name;
        std::string qualified_name;
        std::string signature;
        std::string return_type;
        std::size_t start_line;
        int brace_depth;
    };
    std::vector<FunctionScope> active_functions;

    int current_brace_depth = 0;

    for (std::size_t i = 0; i < num_tokens; ++i) {
        const auto& tok = tokens[i];

        // Track braces
        if (tok.kind == TokenKind::Punctuation && tok.text == "{") {
            ++current_brace_depth;
            continue;
        }
        if (tok.kind == TokenKind::Punctuation && tok.text == "}") {
            --current_brace_depth;

            // Check if active function finished
            if (!active_functions.empty() &&
                current_brace_depth == active_functions.back().brace_depth) {
                const auto& fn = active_functions.back();
                model.functions.push_back({
                    fn.name, fn.qualified_name, fn.signature,
                    fn.return_type, fn.start_line, tok.line
                });
                active_functions.pop_back();
            }

            // Check if active class finished
            if (!active_classes.empty() &&
                current_brace_depth == active_classes.back().brace_depth) {
                const auto& cls = active_classes.back();
                model.classes.push_back({cls.name, cls.start_line, tok.line});
                if (!class_stack.empty()) class_stack.pop_back();
                active_classes.pop_back();
            }
            continue;
        }

        // Detect Class or Struct Definition
        if (tok.kind == TokenKind::Identifier && (tok.text == "class" || tok.text == "struct")) {
            // Ensure not preceded by 'enum'
            if (i > 0 && tokens[i - 1].kind == TokenKind::Identifier && tokens[i - 1].text == "enum") {
                continue;
            }
            // Look ahead for class name and '{'
            std::size_t look = i + 1;
            std::string class_name;
            bool is_definition = false;
            while (look < num_tokens) {
                const auto& t = tokens[look];
                if (t.text == ";") {
                    // Forward declaration
                    break;
                }
                if (t.text == "{") {
                    is_definition = true;
                    break;
                }
                if (class_name.empty() && t.kind == TokenKind::Identifier) {
                    // Skip keywords like alignas, final
                    if (t.text != "final" && t.text != "alignas") {
                        class_name = t.text;
                    }
                }
                ++look;
            }
            if (is_definition && !class_name.empty()) {
                class_stack.push_back(class_name);
                active_classes.push_back({class_name, tok.line, current_brace_depth});
                // Note: the '{' at `tokens[look]` will increment current_brace_depth when loop reaches it
            }
            continue;
        }

        // Detect Function Calls inside active function
        if (!active_functions.empty() &&
            tok.kind == TokenKind::Identifier &&
            i + 1 < num_tokens && tokens[i + 1].text == "(") {
            // Check if keyword
            if (control_keywords.find(tok.text) == control_keywords.end()) {
                model.calls.push_back({
                    active_functions.back().qualified_name,
                    tok.text,
                    tok.line
                });
            }
        }

        // Detect Function Definition (only at file/namespace or class scope, not inside an active function)
        if (active_functions.empty() && tok.kind == TokenKind::Punctuation && tok.text == "(" && i > 0) {
            // The candidate function name is tokens[i - 1]
            const auto& name_tok = tokens[i - 1];
            if (name_tok.kind != TokenKind::Identifier ||
                control_keywords.find(name_tok.text) != control_keywords.end()) {
                continue;
            }

            // Find full qualified name (e.g., Class::method)
            std::string func_name = name_tok.text;
            std::string qualified_name = func_name;
            std::size_t name_start_idx = i - 1;

            if (name_start_idx >= 2 &&
                tokens[name_start_idx - 1].text == "::" &&
                tokens[name_start_idx - 2].kind == TokenKind::Identifier) {
                qualified_name = tokens[name_start_idx - 2].text + "::" + func_name;
                name_start_idx -= 2;
            } else if (!class_stack.empty()) {
                qualified_name = class_stack.back() + "::" + func_name;
            }

            // Extract return type (tokens before name back to previous statement delimiter)
            std::string return_type;
            bool invalid_return_type = false;
            if (name_start_idx > 0) {
                std::size_t r = name_start_idx;
                while (r > 0) {
                    const auto& prev = tokens[r - 1];
                    if (prev.text == ";" || prev.text == "{" || prev.text == "}" ||
                        prev.text == ":" || prev.text == "public" || prev.text == "private" ||
                        prev.text == "protected") {
                        break;
                    }
                    --r;
                }
                for (std::size_t rt_idx = r; rt_idx < name_start_idx; ++rt_idx) {
                    if (control_keywords.find(tokens[rt_idx].text) != control_keywords.end()) {
                        invalid_return_type = true;
                        break;
                    }
                    if (!return_type.empty() && tokens[rt_idx].text != "::" &&
                        tokens[rt_idx - 1].text != "::") {
                        return_type += " ";
                    }
                    return_type += tokens[rt_idx].text;
                }
            }
            if (invalid_return_type) continue;
            return_type = trim(return_type);

            // Find matching ')'
            std::size_t look = i + 1;
            int paren_depth = 1;
            while (look < num_tokens && paren_depth > 0) {
                if (tokens[look].text == "(") ++paren_depth;
                else if (tokens[look].text == ")") --paren_depth;
                ++look;
            }
            if (paren_depth != 0) continue;
            std::size_t closing_paren_idx = look - 1;

            // Check what follows the parameter list
            std::string qualifiers;
            bool is_func_def = false;
            std::size_t scan = closing_paren_idx + 1;

            while (scan < num_tokens) {
                const auto& t = tokens[scan];
                if (t.text == ";") {
                    // Function declaration, not definition
                    break;
                }
                if (t.text == "=") {
                    // Pure virtual, default, or delete
                    break;
                }
                if (t.text == "{") {
                    is_func_def = true;
                    break;
                }
                if (t.text == "const" || t.text == "noexcept" || t.text == "override" ||
                    t.text == "final") {
                    if (!qualifiers.empty()) qualifiers += " ";
                    qualifiers += t.text;
                } else if (t.text == ":" || t.text == "->") {
                    // Constructor initializer list or trailing return type
                    while (scan < num_tokens && tokens[scan].text != "{" && tokens[scan].text != ";") {
                        ++scan;
                    }
                    if (scan < num_tokens && tokens[scan].text == "{") {
                        is_func_def = true;
                    }
                    break;
                } else {
                    // Any unexpected operator or token means this is not a definition
                    break;
                }
                ++scan;
            }

            if (is_func_def) {
                // Construct signature
                std::string sig;
                if (!return_type.empty()) sig += return_type + " ";
                sig += func_name + "(";
                for (std::size_t p = i + 1; p < closing_paren_idx; ++p) {
                    if (tokens[p].text != "," && tokens[p].text != "::" &&
                        tokens[p - 1].text != "::" && tokens[p - 1].text != "(") {
                        sig += " ";
                    }
                    sig += tokens[p].text;
                }
                sig += ")";
                if (!qualifiers.empty()) sig += " " + qualifiers;

                active_functions.push_back({
                    func_name,
                    qualified_name,
                    sig,
                    return_type,
                    name_tok.line,
                    current_brace_depth
                });
            }
        }
    }

    return model;
}

} // namespace odysseus::ingestion
