#pragma once

#include "schemas/code.hpp"
#include <string>

namespace odysseus::ingestion {

class CodeExtractor {
public:
    static code::FileModel extract(const std::string& source_text);
};

} // namespace odysseus::ingestion
