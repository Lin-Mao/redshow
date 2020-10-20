#ifndef REDSHOW_GRAPHVIZ_H
#define REDSHOW_GRAPHVIZ_H

#include <string>
#include <map>

struct redshow_graphviz_node {
  int32_t node_id;
  std::string context;
  std::string type;
  // node_id,partial;
  std::string duplicate;
  double redundancy;
  double overwrite;

  redshow_graphviz_node() = default;

  redshow_graphviz_node(int32_t node_id, const std::string &type, const std::string &duplicate, double redundancy, double overwrite)
      : node_id(node_id), type(type), duplicate(duplicate), redundancy(redundancy), overwrite(overwrite) {}
};

struct redshow_graphviz_edge {
  std::string type;

  redshow_graphviz_edge() = default;
  redshow_graphviz_edge(const std::string &type) : type(type) {}
};

#endif  // REDSHOW_GRAPHVIZ_H
