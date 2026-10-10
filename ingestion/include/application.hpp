#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include "github_adapter.hpp"

namespace odysseus::ingestion {

enum class OutputMode { Print, Postgres };

struct ApplicationConfig {
    OutputMode output = OutputMode::Print;
    bool mock = false;
    std::string database_url;
    std::string github_token;
};

class IngestionApplication {
public:
    IngestionApplication(std::unique_ptr<GitHubClient> client,
                         std::unique_ptr<StorageManager> storage);
    IngestionResult ingest(const SourceRequest& request);

private:
    std::unique_ptr<GitHubClient> client_;
    std::unique_ptr<StorageManager> storage_;
    ArtifactProcessor processor_;
};

// The output stream must outlive the application when using Print mode.
IngestionApplication build_application(const ApplicationConfig& config,
                                       std::ostream& output);

} // namespace odysseus::ingestion
