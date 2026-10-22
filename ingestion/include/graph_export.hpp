#pragma once
#include "schemas/graph.hpp"
#include <filesystem>

namespace odysseus::ingestion {
// Convert local export envelopes. Requires code schema v2; documents have no schema version.
// Throws for malformed records, conflicting identities, or dangling caller references.
graph::Graph build_graph(const std::vector<json>& records);
graph::Graph export_graph(const std::filesystem::path& input_directory,
                          const std::filesystem::path& output_file);
}
