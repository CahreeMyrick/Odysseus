#include "content_classifier.hpp"

#include <filesystem>
#include <stdexcept>

namespace odysseus::ingestion {

bool ContentClassifier::supports_path(const std::string& path) {
    const auto ext = std::filesystem::path(path).extension().string();
    return ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" ||
           ext == ".h" || ext == ".c" || ext == ".md" || ext == ".txt";
}

ClassificationResult ContentClassifier::classify(Artifact& artifact) const {
    const auto extension = std::filesystem::path(artifact.location.path).extension().string();
    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx" ||
        extension == ".hpp" || extension == ".h" || extension == ".c") {
        artifact.modality = Modality::Code;
        artifact.content.mime_type = "text/plain";
        artifact.metadata["language"] = extension == ".c" ? "C" : "C++";
    } else if (extension == ".md" || extension == ".txt") {
        artifact.modality = Modality::Document;
        artifact.content.mime_type = extension == ".md" ? "text/markdown" : "text/plain";
    } else {
        return ClassificationResult::UnsupportedFormat;
    }
    if (artifact.content.encoding != ContentEncoding::Utf8 ||
        artifact.content.has_bytes() ||
        artifact.content.text.find('\0') != std::string::npos) {
        throw std::runtime_error("Binary content is unsupported: " + artifact.location.path);
    }
    return ClassificationResult::Supported;
}

} // namespace odysseus::ingestion
