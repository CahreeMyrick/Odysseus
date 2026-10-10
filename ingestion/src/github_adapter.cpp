#include "github_adapter.hpp"

#include <filesystem>
#include <stdexcept>

namespace odysseus::ingestion {

GitHubAdapter::GitHubAdapter(GitHubClient& client, const SourceRequest& request)
    : request_(request), snapshot_(client.fetch_repository(request)) {
    if (snapshot_.revision.empty()) {
        throw std::invalid_argument("Repository snapshot must include a revision");
    }
}

std::optional<Artifact> GitHubAdapter::next() {
    if (index_ == snapshot_.files.size()) return std::nullopt;
    const auto& file = snapshot_.files[index_];
    const auto path = std::filesystem::path(file.path);
    Artifact artifact;
    artifact.id = json::array({request_.uri, snapshot_.revision, file.path}).dump();
    artifact.type = ArtifactType::File;
    artifact.name = path.filename().string();
    artifact.location.path = file.path;
    artifact.size_bytes = file.content.size();
    artifact.provenance.source_id = request_.uri;
    artifact.provenance.snapshot_id = snapshot_.revision;
    artifact.content.text = file.content;
    // The client supplies UTF-8 text; format support is checked downstream.
    artifact.content.encoding = ContentEncoding::Utf8;
    ++index_;
    return artifact;
}

} // namespace odysseus::ingestion
