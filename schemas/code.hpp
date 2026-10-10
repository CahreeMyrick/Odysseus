#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common.hpp"

namespace odysseus::code {

// Byte offsets are zero-based, end-exclusive, in the original UTF-8 input.
// Lines are one-based and inclusive. Missing syntax has a zero-width span.
struct SourceSpan {
    std::size_t start_byte = 0;
    std::size_t end_byte = 0;
    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

struct ParseDiagnostic {
    std::string kind;
    std::string message;
    SourceSpan span;
};

struct LexicalScope {
    std::string id;
    std::string kind;
    std::string name;
    std::string qualified_name;
    std::string owner_id;
    SourceSpan span;
};

struct ParserInfo {
    std::string name;
    std::string runtime_version;
    std::string grammar;
    std::string grammar_version;
    std::string extractor_version;
};

struct Include {
    std::string target;

    std::size_t line = 0;
    SourceSpan span;
    std::string resolution_status = "unresolved";
};

struct ClassDefinition {
    std::string name;

    std::size_t start_line = 0;
    std::size_t end_line = 0;
    std::string id;
    std::string qualified_name;
    std::string owner_id;
    std::string kind;
    SourceSpan span;
};

struct FunctionDefinition {
    std::string name;
    std::string qualified_name;
    std::string signature;
    std::string return_type;

    std::size_t start_line = 0;
    std::size_t end_line = 0;
    std::string id;
    std::string owner_id;
    std::string kind = "function";
    SourceSpan span;
};

struct FunctionCall {
    std::string caller;
    std::string callee;

    std::size_t line = 0;
    std::string id;
    std::string caller_id;
    std::string owner_id;
    std::string expression;
    std::string resolution_status = "unresolved";
    SourceSpan span;
};

struct FileModel {
    FileMetadata file;

    std::vector<Include> includes;
    std::vector<ClassDefinition> classes;
    std::vector<FunctionDefinition> functions;
    std::vector<FunctionCall> calls;
    // IDs are file-local occurrence IDs; consumers must scope them by artifact
    // and extraction version. They do not establish identity across revisions.
    int schema_version = 1;
    ParserInfo parser;
    std::string parse_status = "unparsed";
    std::vector<ParseDiagnostic> diagnostics;
    std::vector<LexicalScope> scopes;
};

}
