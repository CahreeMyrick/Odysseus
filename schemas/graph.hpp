#pragma once
#include "common.hpp"
#include <string>
#include <vector>

namespace odysseus::graph {
struct Node {
    std::string id;
    std::string type;
    json properties = json::object();
};
struct Edge {
    std::string source;
    std::string type;
    std::string target;
    json properties = json::object();
};
struct Graph {
    int schema_version = 1;
    std::vector<Node> nodes;
    std::vector<Edge> edges;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Node, id, type, properties)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Edge, source, type, target, properties)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Graph, schema_version, nodes, edges)
}
