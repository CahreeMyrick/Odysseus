#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

namespace odysseus {

using json = nlohmann::json;

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

struct FileMetadata {
    std::string name;
    std::string path;

    std::string mime_type;
    std::string language;

    std::string content_hash;
    std::size_t size_bytes = 0;

    SourceLocation location;
    Provenance provenance;

    json metadata = json::object();
};

}
