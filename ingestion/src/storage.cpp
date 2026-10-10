#include "storage.hpp"

#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace odysseus::ingestion {

PrintStorage::PrintStorage(std::ostream& output) : output_(output) {}

void PrintStorage::write(const StructuredModel& model) {
    std::visit([&](const auto& parsed) {
        using Model = std::decay_t<decltype(parsed)>;
        constexpr bool code_model = std::is_same_v<Model, code::FileModel>;
        output_ << (code_model ? "[code] " : "[document] ")
                << parsed.file.path << " (" << parsed.file.size_bytes << " bytes)\n"
                << model.artifact.content.text << '\n';
    }, model.model);
    if (!output_) throw std::runtime_error("Failed to write ingestion output");
}

} // namespace odysseus::ingestion
