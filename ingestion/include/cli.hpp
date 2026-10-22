#pragma once

#include <iosfwd>
#include <stdexcept>
#include "ingestion.hpp"

namespace odysseus::ingestion {

struct CliOptions {
    SourceRequest source;
    OutputMode output = OutputMode::Print;
    std::string output_directory;
    bool help = false;
};

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

CliOptions parse_cli(int argc, char* argv[]);
IngestionConfig load_config(const CliOptions& options);
void print_usage(std::ostream& output);
void print_result(std::ostream& output, const IngestionResult& result,
                  const CliOptions& options);

} // namespace odysseus::ingestion
