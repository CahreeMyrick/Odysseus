#include "application.hpp"
#include "rest_github_client.hpp"
#include "mock_github_client.hpp"
#include "postgres_storage.hpp"

#include <stdexcept>
#include <utility>

namespace odysseus::ingestion {

IngestionApplication::IngestionApplication(std::unique_ptr<GitHubClient> client,
                                           std::unique_ptr<StorageManager> storage)
    : client_(std::move(client)), storage_(std::move(storage)) {
    if (!client_ || !storage_) {
        throw std::invalid_argument("Application requires a source client and storage");
    }
}

IngestionResult IngestionApplication::ingest(const SourceRequest& request) {
    // Construct reference-holding objects here so moving the application is safe.
    AdapterFactory factory;
    factory.register_source(SourceType::GitHub, [this](const SourceRequest& source) {
        return std::make_unique<GitHubAdapter>(*client_, source);
    });
    IngestionPipeline pipeline(factory, processor_, *storage_);
    return pipeline.ingest(request);
}

IngestionApplication build_application(const ApplicationConfig& config, std::ostream& output) {
    std::unique_ptr<GitHubClient> client;
    if (config.mock) client = std::make_unique<MockGitHubClient>(example_repository());
    else client = std::make_unique<RestGitHubClient>(std::make_shared<CurlTransport>(), config.github_token);
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
    return IngestionApplication(std::move(client), std::move(storage));
}

} // namespace odysseus::ingestion
