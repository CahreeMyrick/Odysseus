#pragma once
#include "schemas/graph.hpp"

namespace odysseus::ingestion {
// Enrich a complete corpus with explicit syntax associations and inferred
// candidates. Neither evidence category asserts compiler-verified CALLS.
void infer_call_targets(graph::Graph& graph);
}
