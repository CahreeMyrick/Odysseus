#include "pipeline.hpp"
#include "content_classifier.hpp"

#include <stdexcept>
#include <utility>

namespace odysseus::ingestion {

void AdapterFactory::register_source(SourceType type, Creator creator) {
    if (!creator) {
        throw std::invalid_argument("Source creator must not be empty");
    }
    if (!creators_.emplace(type, std::move(creator)).second) {
        throw std::invalid_argument("Source type is already registered");
    }
}

std::unique_ptr<SourceAdapter> AdapterFactory::create(const SourceRequest& request) const {
    if (request.uri.empty()) {
        throw std::invalid_argument("Source URI must not be empty");
    }
    const auto found = creators_.find(request.type);
    if (found == creators_.end()) {
        throw std::invalid_argument("Unsupported source type: no adapter registered");
    }
    auto adapter = found->second(request);
    if (!adapter) {
        throw std::runtime_error("Source creator returned no adapter");
    }
    return adapter;
}

IngestionPipeline::IngestionPipeline(const AdapterFactory& factory,
                                   const ArtifactProcessor& processor,
                                   StorageManager& storage)
    : factory_(factory), processor_(processor), storage_(storage) {}

IngestionResult IngestionPipeline::ingest(const SourceRequest& request) const {
    IngestionResult result;
    auto adapter = factory_.create(request);
    ContentClassifier classifier;
    while (auto artifact = adapter->next()) {
        if (classifier.classify(*artifact) == ClassificationResult::UnsupportedFormat) {
            ++result.artifacts_skipped;
            continue;
        }
        storage_.write(processor_.process(*artifact));
        ++result.artifacts_stored;
    }
    return result;
}

} // namespace odysseus::ingestion
