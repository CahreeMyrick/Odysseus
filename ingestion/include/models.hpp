#pragma once

#include <variant>
#include "schemas/ingestion.hpp"
#include "schemas/code.hpp"
#include "schemas/document.hpp"

namespace odysseus::ingestion {

// Retain the original content and provenance alongside the parsed representation.
struct StructuredModel {
    Artifact artifact;
    std::variant<code::FileModel, document::FileModel> model;
};

struct IngestionResult {
    std::size_t artifacts_stored = 0;
    std::size_t artifacts_skipped = 0;
};

} // namespace odysseus::ingestion
