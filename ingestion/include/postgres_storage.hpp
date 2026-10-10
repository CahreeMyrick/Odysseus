#pragma once

#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include "storage.hpp"

namespace odysseus::ingestion {

enum class IndexTarget { Vector, Graph };

struct IndexingTask {
    std::int64_t id;
    std::int64_t claim_token;
    std::int64_t generation;
    int attempts;
    std::string artifact_id;
    IndexTarget target;
    json payload;
};

// One connection per instance; do not share an instance between threads.
class PostgresStorage final : public StorageManager {
public:
    explicit PostgresStorage(const std::string& connection_string);
    ~PostgresStorage() override;
    PostgresStorage(const PostgresStorage&) = delete;
    PostgresStorage& operator=(const PostgresStorage&) = delete;

    // Creates missing tables, including the additive indexing-task schema.
    void initialize_schema();
    void write(const StructuredModel& model) override;

    // Claims pending, retryable failed, or expired tasks atomically across workers.
    std::optional<IndexingTask> claim_task(IndexTarget target, int lease_seconds = 300);
    // Return false if the lease expired, was reclaimed, or the payload changed.
    bool complete_task(const IndexingTask& task);
    bool fail_task(const IndexingTask& task, const std::string& safe_error,
                   int retry_delay_seconds = 60);
    bool renew_task(const IndexingTask& task, int lease_seconds = 300);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace odysseus::ingestion
