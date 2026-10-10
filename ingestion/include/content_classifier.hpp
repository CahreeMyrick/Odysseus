#pragma once

#include "models.hpp"

namespace odysseus::ingestion {

enum class ClassificationResult { Supported, UnsupportedFormat };

// Classifies supported file formats independently of their acquisition source.
// Requires client-supplied UTF-8 text; does not decode or validate UTF-8 bytes.
class ContentClassifier {
public:
    static bool supports_path(const std::string& path);
    ClassificationResult classify(Artifact& artifact) const;
};

} // namespace odysseus::ingestion
