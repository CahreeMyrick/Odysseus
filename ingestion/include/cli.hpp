#pragma once

#include <iosfwd>
#include <stdexcept>
#include "application.hpp"

namespace odysseus::ingestion {

struct CliOptions {
    SourceRequest source;
    OutputMode output = OutputMode::Print;
    bool mock = false;
    bool help = false;
};

class CliError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

CliOptions parse_cli(int argc, char* argv[]);
ApplicationConfig load_config(const CliOptions& options);
void print_usage(std::ostream& output);
void print_result(std::ostream& output, const IngestionResult& result,
                  const CliOptions& options);

} // namespace odysseus::ingestion
