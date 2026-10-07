// pipeline.cpp

#include "pipeline.hpp"
#include "schema.hpp"

SourceType parse_source_type(const std::string& value) {
    if (value == "github") {
        return SourceType::GitHub;
    }

    if (value == "document") {
        return SourceType::Document;
    }

    return SourceType::Unknown;
}

IngestionPipeline::IngestionPipeline(AdapterFactory adapter, ArtifactProcessor processor, Storage storage) : adapter_factory_(adapter), processor_(processor), storage_(storage) {}

IngestionResult IngestionPipeline::ingest(const SourceRequest& request) {
    IngestionResult result;

    auto adapter = adapter_factory_.create(request);
    
    while (adapter->has_next()) {
        Artifact artifact = adapter->acquire();

        SturcturedModel model = processor_.process(artifact);

        storage_.write(model);
    }

    return result;

}
