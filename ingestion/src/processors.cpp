#include "pipeline.hpp"
#include "code_extractor.hpp"

#include <stdexcept>

namespace odysseus::ingestion {
namespace {
FileMetadata metadata(const Artifact& artifact) {
    FileMetadata result;
    result.name = artifact.name;
    result.path = artifact.location.path;
    result.mime_type = artifact.content.mime_type;
    result.language = artifact.metadata.value("language", std::string{});
    result.content_hash = artifact.content_hash;
    result.size_bytes = artifact.size_bytes;
    result.location = artifact.location;
    result.provenance = artifact.provenance;
    result.metadata = artifact.metadata;
    return result;
}
void require_text(const Artifact& artifact) {
    // Empty text files are valid; representation, not length, determines support.
    if (artifact.content.encoding != ContentEncoding::Utf8 || artifact.content.has_bytes()) {
        throw std::runtime_error("Processor requires UTF-8 text: " + artifact.location.path);
    }
}
}

code::FileModel CodeProcessor::process(const Artifact& artifact) const {
    require_text(artifact);
    code::FileModel model = CodeExtractor::extract(artifact.content.text);
    model.file = metadata(artifact);
    model.file.metadata["syntax_extracted"] = true;
    return model;
}

document::FileModel DocumentProcessor::process(const Artifact& artifact) const {
    require_text(artifact);
    document::FileModel model;
    model.file = metadata(artifact);
    model.sections.push_back({"", artifact.content.text, 0, 0});
    return model;
}

StructuredModel ArtifactProcessor::process(const Artifact& artifact) const {
    switch (artifact.modality) {
        case Modality::Code:
            return {artifact, code_processor_.process(artifact)};
        case Modality::Document:
            return {artifact, document_processor_.process(artifact)};
        default:
            throw std::runtime_error("Unsupported artifact modality: " + artifact.location.path);
    }
}

} // namespace odysseus::ingestion
