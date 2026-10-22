#include "mock_github_client.hpp"
#include "content_classifier.hpp"
#include "cli.hpp"

#include <iostream>
#include <fstream>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <utility>

using namespace odysseus::ingestion;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void expect_error(F action, const std::string& expected) {
    try { action(); }
    catch (const std::exception& error) {
        check(std::string(error.what()).find(expected) != std::string::npos,
              "Unexpected error message");
        return;
    }
    throw std::runtime_error("Expected an exception");
}
struct RecordingStorage final : StorageManager {
    std::vector<StructuredModel> models;
    std::size_t fail_at = static_cast<std::size_t>(-1);
    void write(const StructuredModel& model) override {
        if (models.size() == fail_at) throw std::runtime_error("storage failure");
        models.push_back(model);
    }
};
SourceRequest request_for(const std::string& uri = "https://github.com/example/mock-repo") {
    SourceRequest request;
    request.type = SourceType::GitHub;
    request.uri = uri;
    return request;
}
int main() {
    try {
        std::ostringstream placeholder_output;
        auto initialized_service = initialize_service(IngestionConfig{}, placeholder_output);
        char program[] = "odysseus", command[] = "ingest", uri[] = "example";
        char local[] = "local-file", database[] = "database", unknown[] = "unknown";
        for (auto source : {local, database}) {
            char* args[] = {program, command, source, uri};
            const auto options = parse_cli(4, args);
            const std::string expected = options.source.type == SourceType::LocalFile
                ? "Local-file ingestion is not implemented yet"
                : "Database ingestion is not implemented yet";
            expect_error([&] { initialized_service.ingest(options.source); }, expected);
        }
        check(placeholder_output.str().empty(), "Unimplemented source wrote output");
        char* unknown_args[] = {program, command, unknown, uri};
        expect_error([&] { parse_cli(4, unknown_args); }, "Unknown source type");
        std::ostringstream service_output;
        auto service_client = std::make_shared<MockGitHubClient>(example_repository());
        SourceFactory service_factory;
        service_factory.register_source(SourceType::GitHub,
            [service_client](const SourceRequest& source) {
                return std::make_unique<GitHubAdapter>(*service_client, source);
            });
        Ingestion service(std::move(service_factory), std::make_unique<PrintStorage>(service_output));
        auto moved_service = std::move(service);
        check(moved_service.ingest(request_for()).artifacts_stored == 4,
              "Moved ingestion service failed");
        check(moved_service.ingest(request_for()).artifacts_stored == 4,
              "Repeated ingestion must create a fresh adapter");
        expect_error([] { Ingestion invalid(SourceFactory{}, nullptr); },
                     "requires storage");
        auto client = example_repository();
        SourceFactory factory;
        factory.register_source(SourceType::GitHub, [&client](const SourceRequest& source) {
            return std::make_unique<GitHubAdapter>(client, source);
        });
        ArtifactProcessor processor;
        RecordingStorage storage;
        IngestionPipeline pipeline(factory, processor, storage);
        auto request = request_for();
        SourceFactory unregistered;
        expect_error([&] { unregistered.create(request); }, "no adapter registered");
        expect_error([&] { unregistered.register_source(SourceType::GitHub, {}); },
                     "must not be empty");
        expect_error([&] {
            factory.register_source(SourceType::GitHub, [&client](const SourceRequest& source) {
                return std::make_unique<GitHubAdapter>(client, source);
            });
        }, "already registered");
        SourceFactory invalid_creator;
        invalid_creator.register_source(SourceType::GitHub, [](const SourceRequest&) {
            return std::unique_ptr<SourceAdapter>{};
        });
        expect_error([&] { invalid_creator.create(request); }, "returned no adapter");

        auto raw = factory.create(request)->next();
        check(raw->modality == Modality::Unknown, "Adapter classified content");
        ContentClassifier classifier;
        classifier.classify(*raw);
        check(raw->modality == Modality::Code && raw->metadata.at("language") == "C++",
              "Shared code classification failed");
        Artifact text;
        text.location.path = "README.md";
        text.content.encoding = ContentEncoding::Utf8;
        classifier.classify(text);
        check(text.modality == Modality::Document && text.content.mime_type == "text/markdown",
              "Empty Markdown classification failed");
        text.content.text = std::string("a\0b", 3);
        expect_error([&] { classifier.classify(text); }, "Binary content is unsupported");
        text.content.text.clear();
        text.content.bytes.push_back(std::byte{1});
        expect_error([&] { classifier.classify(text); }, "Binary content is unsupported");

        auto result = pipeline.ingest(request);
        check(result.artifacts_stored == 4 && storage.models.size() == 4, "Missing artifacts");
        check(std::holds_alternative<odysseus::code::FileModel>(storage.models[0].model), "Code dispatch failed");
        check(std::holds_alternative<odysseus::document::FileModel>(storage.models[2].model), "Document dispatch failed");
        check(storage.models[0].artifact.content.text.find("int main()") != std::string::npos, "Lost source content");
        check(storage.models[0].artifact.provenance.snapshot_id == "mock-commit-1", "Lost snapshot");
        const auto& doc = std::get<odysseus::document::FileModel>(storage.models[2].model);
        check(doc.sections.at(0).text == storage.models[2].artifact.content.text, "Lost document text");
        check(storage.models[3].artifact.content.text.empty(), "Empty file rejected");
        const auto first_id = storage.models[0].artifact.id;
        result = pipeline.ingest(request);
        check(result.artifacts_stored == 4 && storage.models.size() == 8, "Adapter reused exhausted state");
        check(storage.models[4].artifact.id == first_id, "Unstable identity on retry");
        auto first = factory.create(request);
        auto second = factory.create(request);
        check(first.get() != second.get(), "Factory reused instance");
        check(first->next()->name == second->next()->name, "Adapters share cursor");
        for (int i = 0; i < 3; ++i) first->next();
        check(!first->next() && !first->next(), "End of stream not stable");
        storage.models.clear();
        storage.fail_at = 1;
        expect_error([&] { pipeline.ingest(request); }, "storage failure");
        check(storage.models.size() == 1, "Did not stop on storage failure");
        storage.fail_at = static_cast<std::size_t>(-1);
        check(pipeline.ingest(request).artifacts_stored == 4, "Cannot retry after failure");
        request.type = SourceType::LocalFile;
        expect_error([&] { factory.create(request); }, "Unsupported source type");
        expect_error([&] { parse_source_type("document"); }, "Unknown source type");
        expect_error([&] { factory.create(request_for("")); }, "must not be empty");
        expect_error([&] { factory.create(request_for("missing")); }, "not found");
        MockGitHubClient empty("empty", {"revision", {}});
        SourceFactory empty_factory;
        empty_factory.register_source(SourceType::GitHub, [&empty](const SourceRequest& source) {
            return std::make_unique<GitHubAdapter>(empty, source);
        });
        IngestionPipeline empty_pipeline(empty_factory, processor, storage);
        check(empty_pipeline.ingest(request_for("empty")).artifacts_stored == 0, "Empty repo failed");
        MockGitHubClient bad("bad", {"revision", {{"ok.cpp", ""}, {"image.png", "bytes"}, {"later.cpp", ""}}});
        SourceFactory bad_factory;
        bad_factory.register_source(SourceType::GitHub, [&bad](const SourceRequest& source) {
            return std::make_unique<GitHubAdapter>(bad, source);
        });
        RecordingStorage partial;
        IngestionPipeline bad_pipeline(bad_factory, processor, partial);
        const auto mixed_result = bad_pipeline.ingest(request_for("bad"));
        check(mixed_result.artifacts_stored == 2 && mixed_result.artifacts_skipped == 1,
              "Mixed repository counts are wrong");
        check(partial.models.size() == 2 && partial.models[1].artifact.name == "later.cpp",
              "Did not continue after unsupported file");
        std::ostringstream summary;
        print_result(summary, mixed_result, CliOptions{});
        check(summary.str().find("Skipped 1 unsupported files") != std::string::npos,
              "CLI did not report skipped files");
        MockGitHubClient unsupported_client("unsupported", {"revision", {{"image.png", "bytes"}}});
        SourceFactory unsupported_factory;
        unsupported_factory.register_source(SourceType::GitHub,
            [&unsupported_client](const SourceRequest& source) {
                return std::make_unique<GitHubAdapter>(unsupported_client, source);
            });
        RecordingStorage unsupported_storage;
        IngestionPipeline unsupported_pipeline(unsupported_factory, processor, unsupported_storage);
        const auto skipped_result = unsupported_pipeline.ingest(request_for("unsupported"));
        check(skipped_result.artifacts_stored == 0 && skipped_result.artifacts_skipped == 1 &&
              unsupported_storage.models.empty(), "Unsupported-only repository failed");
        MockGitHubClient binary_client("binary", {"revision", {
            {"first.cpp", ""}, {"binary.txt", std::string("a\0b", 3)}, {"last.md", ""}}});
        SourceFactory binary_factory;
        binary_factory.register_source(SourceType::GitHub,
            [&binary_client](const SourceRequest& source) {
                return std::make_unique<GitHubAdapter>(binary_client, source);
            });
        RecordingStorage binary_storage;
        IngestionPipeline binary_pipeline(binary_factory, processor, binary_storage);
        expect_error([&] { binary_pipeline.ingest(request_for("binary")); },
                     "Binary content is unsupported");
        check(binary_storage.models.size() == 1, "Invalid text content did not stop ingestion");
        Artifact unsupported;
        expect_error([&] { processor.process(unsupported); }, "Unsupported artifact modality");
        unsupported.modality = Modality::Code;
        unsupported.content.encoding = ContentEncoding::Binary;
        expect_error([&] { processor.process(unsupported); }, "requires UTF-8");
        std::ostringstream output;
        PrintStorage printer(output);
        printer.write(partial.models[0]);
        check(output.str().find("[code] ok.cpp") != std::string::npos, "Print storage failed");
        output.setstate(std::ios::badbit);
        expect_error([&] { printer.write(partial.models[0]); }, "Failed to write");
        const auto export_dir = std::filesystem::temp_directory_path() /
            ("odysseus-local-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); }
        } cleanup{export_dir};
        LocalStorage local_storage(export_dir.string());
        expect_error([&] { LocalStorage duplicate(export_dir.string()); }, "already exists");
        IngestionPipeline local_pipeline(factory, processor, local_storage);
        check(local_pipeline.ingest(request_for()).artifacts_stored == 4, "Local export count failed");
        std::ifstream code_file(export_dir / "1.json");
        odysseus::json code_record;
        code_file >> code_record;
        check(code_record.at("model_type") == "code", "Local model type missing");
        check(code_record.at("artifact_id") == first_id, "Local artifact identity lost");
        check(!code_record.at("model").at("functions").empty(), "Local functions lost");
        check(code_record.at("model").at("parser").at("name") == "tree-sitter", "Local parser lost");
        check(code_record.at("model").at("file").at("provenance").at("snapshot_id") == "mock-commit-1",
              "Local provenance lost");
        std::ifstream doc_file(export_dir / "3.json");
        odysseus::json doc_record;
        doc_file >> doc_record;
        check(doc_record.at("model_type") == "document" &&
              doc_record.at("model").at("sections").at(0).at("text") == "# Mock repository\nA small ingestion fixture.\n",
              "Local document lost");
        char github[] = "github", local_flag[] = "--local", directory[] = "export", postgres[] = "--postgres";
        char* local_args[] = {program, command, github, uri, local_flag, directory, postgres};
        const auto local_options = parse_cli(6, local_args);
        check(local_options.output == OutputMode::Local &&
              load_config(local_options).output_directory == "export", "Local CLI configuration failed");
        expect_error([&] { parse_cli(5, local_args); }, "requires an output directory");
        expect_error([&] { parse_cli(7, local_args); }, "Choose only one output");
        char* reverse_args[] = {program, command, github, uri, postgres, local_flag, directory};
        expect_error([&] { parse_cli(7, reverse_args); }, "Choose only one output");
        std::ostringstream local_summary;
        print_result(local_summary, {4, 0}, local_options);
        check(local_summary.str().find("Saved 4 models to export") != std::string::npos,
              "Local summary failed");
        std::cout << "All pipeline checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
