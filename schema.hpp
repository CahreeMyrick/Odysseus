#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

enum class ArtifactType {
    File,
    Document,
    Unknown
};

enum class Modality {
    Code,
    Document,
    Unknown
};

enum class ContentEncoding {
    Utf8,
    Binary,
    Unknown
};

struct SourceLocation {
    std::string path;
    std::size_t start_line = 0;
    std::size_t end_line = 0;
    std::size_t page = 0;
};

struct Provenance {
    std::string source_id;
    std::string snapshot_id;
    std::string retrieved_at;

    json metadata = json::object();
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
    std::string source_id;
    std::string snapshot_id;

    ArtifactType type = ArtifactType::Unknown;
    Modality modality = Modality::Unknown;

    std::string name;
    ArtifactContent content;

    std::string content_hash;
    std::size_t size_bytes = 0;

    SourceLocation location;

    json metadata = json::object();

    Provenance provenance;
};
