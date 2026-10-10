#pragma once
#include "github_adapter.hpp"
#include "http_transport.hpp"
namespace odysseus::ingestion {
class RestGitHubClient final : public GitHubClient {
public:
    explicit RestGitHubClient(std::shared_ptr<HttpTransport> transport, std::string token = {});
    GitHubSnapshot fetch_repository(const SourceRequest& request) override;
private:
    std::string get(const std::string& path, bool raw = false);
    std::shared_ptr<HttpTransport> transport_;
    std::string token_;
};
}
