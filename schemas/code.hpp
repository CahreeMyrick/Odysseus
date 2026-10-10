#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common.hpp"

namespace odysseus::code {

struct Include {
    std::string target;

    std::size_t line = 0;
};

struct ClassDefinition {
    std::string name;

    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

struct FunctionDefinition {
    std::string name;
    std::string qualified_name;
    std::string signature;
    std::string return_type;

    std::size_t start_line = 0;
    std::size_t end_line = 0;
};

struct FunctionCall {
    std::string caller;
    std::string callee;

    std::size_t line = 0;
};

struct FileModel {
    FileMetadata file;

    std::vector<Include> includes;
    std::vector<ClassDefinition> classes;
    std::vector<FunctionDefinition> functions;
    std::vector<FunctionCall> calls;
};

}
