#include "storage.hpp"
#include "model_json.hpp"
#include <fstream>

#include <ostream>
#include <stdexcept>
#include <type_traits>

namespace odysseus::ingestion {

LocalStorage::LocalStorage(const std::string& directory) : directory_(directory) {
    if (directory.empty()) throw std::invalid_argument("Local output directory must not be empty");
    const auto parent = directory_.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    if (!std::filesystem::create_directory(directory_))
        throw std::runtime_error("Local output directory already exists: " + directory);
}

void LocalStorage::write(const StructuredModel& structured) {
    const auto record = std::visit([&](const auto& parsed) {
        using Model = std::decay_t<decltype(parsed)>;
        return json{{"artifact_id", structured.artifact.id},
                    {"model_type", std::is_same_v<Model, code::FileModel> ? "code" : "document"},
                    {"model", parsed}};
    }, structured.model);
    // Generated names avoid interpreting repository paths as local filesystem paths.
    const auto path = directory_ / (std::to_string(next_id_) + ".json");
    std::ofstream output(path, std::ios::binary);
    output << record.dump(2) << '\n';
    output.close();
    if (!output) throw std::runtime_error("Failed to write local model: " + path.string());
    ++next_id_;
}

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
