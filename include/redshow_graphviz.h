#ifndef REDSHOW_GRAPHVIZ_H
#define REDSHOW_GRAPHVIZ_H

#include <map>
#include <string>

struct redshow_graphviz_node {
  int32_t node_id;
  std::string context;
  std::string type;
  // node_id,partial;
  std::string duplicate;
  uint64_t count;  // total invocations

  redshow_graphviz_node() = default;

  redshow_graphviz_node(int32_t node_id, const std::string &type, const std::string &duplicate, uint64_t count)
      : node_id(node_id), type(type), duplicate(duplicate), count(count) {}
};

struct redshow_graphviz_edge {
  std::string type;
  int32_t memory_node_id;
  double redundancy;
  double overwrite;
  uint64_t count;  // total bytes

  redshow_graphviz_edge() = default;
  redshow_graphviz_edge(const std::string &type) : type(type), redundancy(0.0), overwrite(0.0), count(0) {}
  redshow_graphviz_edge(const std::string &type, int32_t memory_node_id, double redundancy, double overwrite, uint64_t count)
      : type(type), memory_node_id(memory_node_id), redundancy(redundancy), overwrite(overwrite), count(count) {}
};

#endif  // REDSHOW_GRAPHVIZ_H
