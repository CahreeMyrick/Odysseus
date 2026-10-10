#pragma once

#include <stdexcept>
#include <utility>
#include "github_adapter.hpp"

namespace odysseus::ingestion {

// Explicit fixture client: no HTTP requests or credentials.
class MockGitHubClient final : public GitHubClient {
public:
    MockGitHubClient(std::string uri, GitHubSnapshot snapshot)
        : uri_(std::move(uri)), snapshot_(std::move(snapshot)) {}
    GitHubSnapshot fetch_repository(const SourceRequest& request) override {
        if (request.uri != uri_) {
            throw std::invalid_argument("Mock repository not found: " + request.uri);
        }
        return snapshot_;
    }
private:
    std::string uri_;
    GitHubSnapshot snapshot_;
};

inline MockGitHubClient example_repository() {
    return MockGitHubClient("https://github.com/example/mock-repo", {
        "mock-commit-1", {
            {"src/main.cpp", "#include <iostream>\nint main() { std::cout << \"hello\\n\"; }\n"},
            {"include/example.hpp", "#pragma once\nint example();\n"},
            {"README.md", "# Mock repository\nA small ingestion fixture.\n"},
            {"empty.txt", ""}
        }
    });
}

} // namespace odysseus::ingestion
