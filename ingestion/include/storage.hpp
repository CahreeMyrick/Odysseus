#pragma once

#include <iosfwd>
#include "models.hpp"

namespace odysseus::ingestion {

class StorageManager {
public:
    virtual void write(const StructuredModel& model) = 0;
    virtual ~StorageManager() = default;
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
