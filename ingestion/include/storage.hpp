#pragma once

#include <iosfwd>
#include <filesystem>
#include "models.hpp"

namespace odysseus::ingestion {

class StorageManager {
public:
    virtual void write(const StructuredModel& model) = 0;
    virtual ~StorageManager() = default;
};

// One JSON file per artifact in a newly created output directory.
class LocalStorage final : public StorageManager {
public:
    explicit LocalStorage(const std::string& directory);
    void write(const StructuredModel& model) override;
private:
    std::filesystem::path directory_;
    std::size_t next_id_ = 1;
};

// Initial storage implementation; does not write to any database.
class PrintStorage final : public StorageManager {
public:
    explicit PrintStorage(std::ostream& output);
    void write(const StructuredModel& model) override;
private:
    std::ostream& output_;
};

} // namespace odysseus::ingestion
