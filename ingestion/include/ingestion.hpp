#pragma once

#include <iosfwd>
#include <memory>
#include <string>
#include "pipeline.hpp"

namespace odysseus::ingestion {

enum class OutputMode { Print, Postgres };

struct IngestionConfig {
    OutputMode output = OutputMode::Print;
    std::string database_url;
    std::string github_token;
};

class Ingestion {
public:
    Ingestion(SourceFactory source_factory, std::unique_ptr<StorageManager> storage);
    IngestionResult ingest(const SourceRequest& request);

private:
    SourceFactory source_factory_;
    std::unique_ptr<StorageManager> storage_;
    ArtifactProcessor processor_;
};

// The output stream must outlive the ingestion service when using Print mode.
Ingestion initialize_service(const IngestionConfig& config, std::ostream& output);

} // namespace odysseus::ingestion
