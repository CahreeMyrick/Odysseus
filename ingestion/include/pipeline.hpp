#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "models.hpp"
#include "storage.hpp"

namespace odysseus::ingestion {

SourceType parse_source_type(const std::string& value);

// Each instance represents one initialized ingestion session.
class SourceAdapter {
public:
    virtual std::optional<Artifact> next() = 0;
    virtual ~SourceAdapter() = default;
};

class CodeProcessor {
public:
    code::FileModel process(const Artifact& artifact) const;
};

class DocumentProcessor {
public:
    document::FileModel process(const Artifact& artifact) const;
};

class ArtifactProcessor {
public:
    StructuredModel process(const Artifact& artifact) const;

private:
    CodeProcessor code_processor_;
    DocumentProcessor document_processor_;
};

class AdapterFactory {
public:
    using Creator = std::function<std::unique_ptr<SourceAdapter>(const SourceRequest&)>;

    // Captured dependencies must outlive the factory and adapters it creates.
    // Duplicate registrations are rejected rather than silently replaced.
    void register_source(SourceType type, Creator creator);
    std::unique_ptr<SourceAdapter> create(const SourceRequest& request) const;

private:
    std::map<SourceType, Creator> creators_;
};

class IngestionPipeline {
public:
    // Dependencies must outlive the pipeline. Each ingest owns its adapter.
    IngestionPipeline(const AdapterFactory& factory,
                      const ArtifactProcessor& processor, StorageManager& storage);
    // Fail fast: exceptions propagate; earlier writes are not rolled back.
    IngestionResult ingest(const SourceRequest& request) const;

private:
    const AdapterFactory& factory_;
    const ArtifactProcessor& processor_;
    StorageManager& storage_;
};

} // namespace odysseus::ingestion
