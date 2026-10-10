#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "common.hpp"

namespace odysseus::ingestion {

enum class ArtifactType {
    File,
    Unknown
};

enum class Modality {
    Code,
    Document,
    Tabular,
    Image,
    Unknown
};

enum class ContentEncoding {
    Utf8,
    Binary,
    Unknown
};

enum class SourceType {
    GitHub,
    LocalFile,
    Database,
    Unknown
};

struct ArtifactContent {
    std::string text;
    std::vector<std::byte> bytes;

    std::string mime_type;
    ContentEncoding encoding = ContentEncoding::Unknown;

    bool has_text() const {
        return !text.empty();
    }

    bool has_bytes() const {
        return !bytes.empty();
    }
};

struct Artifact {
    std::string id;

    ArtifactType type = ArtifactType::Unknown;
    Modality modality = Modality::Unknown;

    std::string name;

    ArtifactContent content;

    std::string content_hash;
    std::size_t size_bytes = 0;

    SourceLocation location;
    Provenance provenance;

    json metadata = json::object();
};

struct SourceRequest {
    SourceType type = SourceType::Unknown;
    std::string uri;

    json options = json::object();
};

}
