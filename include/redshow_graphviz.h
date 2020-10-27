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

  redshow_graphviz_node() = default;

  redshow_graphviz_node(int32_t node_id, const std::string &type, const std::string &duplicate)
      : node_id(node_id), type(type), duplicate(duplicate) {}
};

struct redshow_graphviz_edge {
  std::string type;
  double redundancy;
  double overwrite;

  redshow_graphviz_edge() = default;
  redshow_graphviz_edge(const std::string &type) : type(type), redundancy(0.0), overwrite(0.0) {}
  redshow_graphviz_edge(const std::string &type, double redundancy, double overwrite)
      : type(type), redundancy(redundancy), overwrite(overwrite) {}
};

#endif  // REDSHOW_GRAPHVIZ_H
