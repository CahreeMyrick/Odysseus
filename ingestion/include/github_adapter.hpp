#pragma once

#include <vector>
#include "pipeline.hpp"

namespace odysseus::ingestion {

struct GitHubFile {
    std::string path;
    std::string content;
};

struct GitHubSnapshot {
    std::string revision;
    std::vector<GitHubFile> files;
};

class GitHubClient {
public:
    virtual GitHubSnapshot fetch_repository(const SourceRequest& request) = 0;
    virtual ~GitHubClient() = default;
};

class GitHubAdapter final : public SourceAdapter {
public:
    GitHubAdapter(GitHubClient& client, const SourceRequest& request);
    std::optional<Artifact> next() override;
private:
    SourceRequest request_;
    GitHubSnapshot snapshot_;
    std::size_t index_ = 0;
};

} // namespace odysseus::ingestion
