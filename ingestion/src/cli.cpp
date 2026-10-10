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
    std::vector<std::string> positional;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.help = true;
        } else if (arg == "--mock") {
            options.mock = true;
        } else if (arg == "--ref") {
            if (++i >= argc || std::string(argv[i]).empty() || std::string(argv[i]).front() == '-')
                throw CliError("--ref requires a branch, tag, or commit");
            options.source.options["ref"] = argv[i];
        } else if (arg == "--postgres") {
            options.output = OutputMode::Postgres;
        } else if (!arg.empty() && arg.front() == '-') {
            throw CliError("Unknown option: " + arg);
        } else {
            positional.push_back(arg);
        }
    }
    if (options.help) return options;
    if (positional.size() != 3 || positional[0] != "ingest") {
        throw CliError("Expected: ingest github <repository-uri>");
    }
    if (positional[1] != "github") {
        throw CliError("Unsupported source: " + positional[1] + "; only github is implemented");
    }
    options.source.type = parse_source_type(positional[1]);
    options.source.uri = positional[2];
    if (options.source.uri.empty()) throw CliError("Repository URI must not be empty");
    if (options.mock && options.source.options.contains("ref"))
        throw CliError("--ref cannot be used with --mock");
    return options;
}

ApplicationConfig load_config(const CliOptions& options) {
    ApplicationConfig config;
    config.output = options.output;
    config.mock = options.mock;
    if (!config.mock) {
        if (const char* token = std::getenv("GITHUB_TOKEN")) config.github_token = token;
    }
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
    output << "Usage: odysseus ingest github <repository-uri> [--ref REVISION] [--mock] [--postgres]\n"
           << "       odysseus --help\n\n"
           << "  --ref       Branch, tag, or commit; defaults to the default branch.\n"
           << "  --mock      Use the built-in repository (no GitHub requests).\n"
           << "              URI: https://github.com/example/mock-repo\n"
           << "  --postgres  Store results using ODYSSEUS_DATABASE_URL.\n"
           << "              Without this flag, print results.\n";
}

void print_result(std::ostream& output, const IngestionResult& result,
                  const CliOptions& options) {
    const bool postgres = options.output == OutputMode::Postgres;
    output << (postgres ? "Stored " : "Printed ") << result.artifacts_stored
           << (postgres ? " artifacts in PostgreSQL.\n" : " artifacts.\n");
    output << "Skipped " << result.artifacts_skipped << " unsupported files.\n";
}

} // namespace odysseus::ingestion
