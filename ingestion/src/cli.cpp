#include "cli.hpp"

#include <cstdlib>
#include <ostream>
#include <vector>

namespace odysseus::ingestion {

SourceType parse_source_type(const std::string& value) {
    if (value == "github") return SourceType::GitHub;
    if (value == "local-file") return SourceType::LocalFile;
    if (value == "database") return SourceType::Database;
    throw std::invalid_argument("Unknown source type: " + value);
}

CliOptions parse_cli(int argc, char* argv[]) {
    CliOptions options;
    if (argc > 1 && std::string(argv[1]) == "graph-export") {
        options.graph_export = true;
        if (argc == 3 && (std::string(argv[2]) == "--help" || std::string(argv[2]) == "-h")) {
            options.help = true;
            return options;
        }
        if (argc != 5 || std::string(argv[3]) != "--output" ||
            std::string(argv[2]).empty() || std::string(argv[2]).front() == '-' ||
            std::string(argv[4]).empty() || std::string(argv[4]).front() == '-')
            throw CliError("Expected: graph-export <input-directory> --output <graph.json>");
        options.graph_input = argv[2];
        options.graph_output = argv[4];
        return options;
    }
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--ref") {
            if (++i >= argc || std::string(argv[i]).empty() || std::string(argv[i]).front() == '-')
                throw CliError("--ref requires a branch, tag, or commit");
            options.source.options["ref"] = argv[i];
        } else if (arg == "--local") {
            if (options.output != OutputMode::Print)
                throw CliError("Choose only one output option: --local or --postgres");
            if (++i >= argc || std::string(argv[i]).empty() || std::string(argv[i]).front() == '-')
                throw CliError("--local requires an output directory");
            options.output = OutputMode::Local;
            options.output_directory = argv[i];
        } else if (arg == "--postgres") {
            if (options.output != OutputMode::Print)
                throw CliError("Choose only one output option: --local or --postgres");
            options.output = OutputMode::Postgres;
        } else if (!arg.empty() && arg.front() == '-') {
            throw CliError("Unknown option: " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    if (options.help) return options;
    if (positional.size() != 3 || positional[0] != "ingest") {
        throw CliError("Expected: ingest <source> <source-uri>");
    }
    try {
        options.source.type = parse_source_type(positional[1]);
    } catch (const std::invalid_argument& error) {
        throw CliError(error.what());
    }
    options.source.uri = positional[2];
    if (options.source.uri.empty()) throw CliError("Source URI must not be empty");
    return options;
}

IngestionConfig load_config(const CliOptions& options) {
    IngestionConfig config;
    config.output = options.output;
    config.output_directory = options.output_directory;
    if (const char* token = std::getenv("GITHUB_TOKEN")) config.github_token = token;
    if (config.output == OutputMode::Postgres) {
        const char* value = std::getenv("ODYSSEUS_DATABASE_URL");
        if (!value || !*value) {
            throw std::invalid_argument("Set ODYSSEUS_DATABASE_URL to use --postgres");
        }
        config.database_url = value;
    }
    return config;
}

void print_usage(std::ostream& output) {
    output << "Usage: odysseus ingest <source> <source-uri> [--ref REVISION] [--postgres | --local DIRECTORY]\n"
           << "       odysseus graph-export <input-directory> --output <graph.json>\n"
           << "       odysseus --help\n\n"
           << "Sources: github, local-file, database\n"
           << "  local-file and database are not implemented yet.\n\n"
           << "  --ref       GitHub branch, tag, or commit; defaults to the default branch.\n"
           << "  --postgres  Store results using ODYSSEUS_DATABASE_URL.\n"
           << "  --local     Save model JSON files in a new directory.\n"
           << "              Without an output option, print source contents.\n";
}

void print_result(std::ostream& output, const IngestionResult& result,
                  const CliOptions& options) {
    const bool postgres = options.output == OutputMode::Postgres;
    if (options.output == OutputMode::Local) {
        output << "Saved " << result.artifacts_stored << " models to "
               << options.output_directory << ".\n";
    } else {
        output << (postgres ? "Stored " : "Printed ") << result.artifacts_stored
               << (postgres ? " artifacts in PostgreSQL.\n" : " artifacts.\n");
    }
    output << "Skipped " << result.artifacts_skipped << " unsupported files.\n";
}

} // namespace odysseus::ingestion
