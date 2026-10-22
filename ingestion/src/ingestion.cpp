#include "ingestion.hpp"
#include "rest_github_client.hpp"
#include "postgres_storage.hpp"

#include <stdexcept>
#include <utility>

namespace odysseus::ingestion {

Ingestion::Ingestion(SourceFactory source_factory, std::unique_ptr<StorageManager> storage) : source_factory_(std::move(source_factory)), storage_(std::move(storage)) {
    if (!storage_) {
        throw std::invalid_argument("Ingestion requires storage");
    }
}

IngestionResult Ingestion::ingest(const SourceRequest& request) {
    IngestionPipeline pipeline(source_factory_, processor_, *storage_);
    return pipeline.ingest(request);
}

Ingestion initialize_service(const IngestionConfig& config, std::ostream& output) {
    auto github_client = std::make_shared<RestGitHubClient>(std::make_shared<CurlTransport>(), config.github_token);
    SourceFactory source_factory;

    source_factory.register_source(SourceType::GitHub, [github_client](const SourceRequest& source) {
        return std::make_unique<GitHubAdapter>(*github_client, source);
    });
    source_factory.register_source(SourceType::LocalFile, [](const SourceRequest&) -> std::unique_ptr<SourceAdapter> {
            throw std::runtime_error("Local-file ingestion is not implemented yet");
    });
    source_factory.register_source(SourceType::Database, [](const SourceRequest&) -> std::unique_ptr<SourceAdapter> {
        throw std::runtime_error("Database ingestion is not implemented yet");
    });

    std::unique_ptr<StorageManager> storage;
    if (config.output == OutputMode::Postgres) {
        if (config.database_url.empty()) {
            throw std::invalid_argument("Set ODYSSEUS_DATABASE_URL to use --postgres");
        }
        auto database = std::make_unique<PostgresStorage>(config.database_url);
        database->initialize_schema();
        storage = std::move(database);
    } else {
        storage = std::make_unique<PrintStorage>(output);
    }
    return Ingestion(std::move(source_factory), std::move(storage));
}

} // namespace odysseus::ingestion
