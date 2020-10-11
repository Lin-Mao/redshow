#ifndef REDSHOW_GRAPHVIZ_H
#define REDSHOW_GRAPHVIZ_H

#include <string>
#include <map>

struct redshow_graphviz_node {
  int32_t node_id;
  std::string context;
  std::string type;
  float redundancy;
  float overwrite;
  std::map<std::string, std::string> property;

  redshow_graphviz_node() = default;
  redshow_graphviz_node(int32_t node_id, const std::string &type, float redundancy)
      : node_id(node_id), type(type), redundancy(redundancy) {}
};

struct redshow_graphviz_edge {
  std::string type;

  redshow_graphviz_edge() = default;
  redshow_graphviz_edge(const std::string &type) : type(type) {}
};

#endif  // REDSHOW_GRAPHVIZ_H
