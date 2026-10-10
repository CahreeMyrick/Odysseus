#pragma once

#include "schemas/code.hpp"
#include <string>

namespace odysseus::ingestion {

enum class CodeLanguage { C, Cpp };

class CodeExtractor {
public:
    static code::FileModel extract(const std::string& source_text,
                                   CodeLanguage language = CodeLanguage::Cpp);
};

} // namespace odysseus::ingestion
