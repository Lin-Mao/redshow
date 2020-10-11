#include "value_flow.h"

#include <map>
#include <queue>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include <boost/graph/graphviz.hpp>

#include "redshow_graphviz.h"
#include "hash.h"
#include "utils.h"

#define DEBUG_VALUE_FLOW 1

namespace redshow {

std::string compute_memory_hash(uint64_t start, uint64_t len) {
  // Use sha256
  return sha256(reinterpret_cast<void *>(start), len);
}

double compute_memcpy_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len) {
  // compare every byte
  double same = 0;

  auto *dst_ptr = reinterpret_cast<unsigned char *>(dst_start);
  auto *src_ptr = reinterpret_cast<unsigned char *>(src_start);

  for (size_t i = 0; i < len; ++i) {
    if (*dst_ptr == *src_ptr) {
      same += 1.0;
    }
  }

  return same / len;
}

double compute_memset_redundancy(uint64_t start, uint32_t value, uint64_t len) {
  // compare every byte
  double same = 0;

  auto *ptr = reinterpret_cast<unsigned char *>(start);

  for (size_t i = 0; i < len; ++i) {
    if (*ptr == static_cast<unsigned char>(value)) {
      same += 1.0;
    }
  }

  return same / len;
}

std::string get_value_flow_node_type(ValueFlowNodeType type) {
  switch (type) {
    case VALUE_FLOW_NODE_ALLOC:
      return "alloc";
    case VALUE_FLOW_NODE_KERNEL:
      return "kernel";
    case VALUE_FLOW_NODE_MEMCPY:
      return "memcpy";
    case VALUE_FLOW_NODE_MEMSET:
      return "memset";
    default:
      return "none"; 
  }
}

std::string get_value_flow_edge_type(ValueFlowEdgeType type) {
  switch (type) {
    case VALUE_FLOW_EDGE_READ:
      return "read";
    case VALUE_FLOW_EDGE_ORDER:
      return "write";
    default:
      return "none"; 
  }
}

static void analyze_duplicate(const std::vector<ValueFlowOp> &value_flow_ops,
                             std::map<int32_t, std::map<int32_t, bool>> &duplicate) {
  std::map<uint32_t, std::set<std::string>> node_hashes;
  for (auto &op : value_flow_ops) {
    node_hashes[op.id].insert(op.hash);
  }

  std::map<std::string, std::map<int32_t, bool>> hash_nodes;
  for (auto &op : value_flow_ops) {
    if (node_hashes[op.id].size() > 1) {
      // Partial duplicate
      hash_nodes[op.hash][op.id] = false;
    } else {
      // Total duplicate
      hash_nodes[op.hash][op.id] = true;
    }
  }

  // Construct duplicate connections
  for (auto &iter : node_hashes) {
    auto node_id = iter.first;
    for (auto &hash : iter.second) {
      for (auto &node_iter : hash_nodes[hash]) {
        auto dup_node_id = node_iter.first;
        auto total = node_iter.second;
        duplicate[node_id][dup_node_id] = total;
        duplicate[dup_node_id][node_id] = total;
      }
    }
  }
}

static void analyze_hot_api(const std::vector<ValueFlowOp> &value_flow_ops,
                            std::map<int32_t, std::pair<double, int>> &hot_apis) {
  for (auto &op : value_flow_ops) {
    if (op.redundancy > 0) {
      auto &redundancy_count = hot_apis[op.id];
      redundancy_count.first += op.redundancy;
      redundancy_count.second += 1;
    }
  }

  // Calculate average
  for (auto &iter : hot_apis) {
    auto &api = iter.second;
    api.first = api.first / api.second;
  }
}

/*
static void backprop_bfs(const ValueFlowGraph &value_flow_graph,
                         const std::map<int32_t, std::pair<double, int>> &hot_apis, int32_t node_id,
                         std::map<int32_t, std::set<int32_t>> &trace_graph) {
  const double BACKPROP_RED = 0.9;

  std::queue<int32_t> queue;
  queue.push(node_id);
  while (queue.empty() == false) {
    auto curr_id = queue.front();
    queue.pop();

    if (value_flow_graph.incoming_nodes_size(curr_id) > 0) {
      auto &incoming_nodes = value_flow_graph.incoming_nodes(curr_id);
      for (auto &iter : incoming_nodes) {
        auto edge_type = iter.first;
        auto out_id = iter.second;

        bool prop = false;
        if (edge_type == VALUE_FLOW_EDGE_READ) {
          prop = true;
        } else {
          const auto &hot_iter = hot_apis.find(out_id);
          if (hot_iter != hot_apis.end() && hot_iter->first > BACKPROP_RED) {
            prop = true;
          }
        }

        if (prop) {
          if (trace_graph.find(curr_id) == trace_graph.end()) {
            queue.push(curr_id);
          }
          trace_graph[curr_id].insert(out_id);
        }
      }
    }
  }
}

static void backprop(const ValueFlowGraph &value_flow_graph,
                     const std::map<int32_t, std::pair<double, int>> &hot_apis,
                     std::map<int32_t, ValueFlowRecord> &value_flow_records) {
  const int HOT_API_COUNT = 1;
  const double HOT_API_RED = 0.3;

  for (auto &iter : hot_apis) {
    auto &node_id = iter.first;
    auto &api = iter.second;
    if (api.first >= HOT_API_RED && api.second >= HOT_API_COUNT) {
      // Backprop patterns
      value_flow_records[node_id].id = node_id;
      auto &trace_graph = value_flow_records[node_id].backtrace;
      backprop_bfs(value_flow_graph, hot_apis, node_id, trace_graph);
    }
  }
}
*/

static void dump_value_flow(const ValueFlowGraph &value_flow_graph,
                            const std::map<int32_t, std::map<int32_t, bool>> &duplicate,
                            const std::map<int32_t, std::pair<double, int>> &hot_apis) {
  typedef redshow_graphviz_node VertexProperty;
  typedef redshow_graphviz_edge EdgeProperty;
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, VertexProperty,
                                EdgeProperty> Graph;
  typedef boost::graph_traits<Graph>::vertex_descriptor vertex_descriptor;
  typedef boost::graph_traits<Graph>::edge_descriptor edge_descriptor;
  Graph g;
  std::map<int32_t, vertex_descriptor> vertice;
  for (auto node_iter = value_flow_graph.nodes_begin(); node_iter != value_flow_graph.nodes_end();
       ++node_iter) {
    auto &node = value_flow_graph.node(node_iter->first);
    if (vertice.find(node.id) == vertice.end()) {
      auto type = get_value_flow_node_type(node.type);
      auto redundancy = 0.0;
      if (hot_apis.find(node.id) != hot_apis.end()) {
        redundancy = hot_apis.at(node.id).first;
      }
      auto v = boost::add_vertex(VertexProperty(node.id, type, redundancy), g);
      vertice[node.id] = v;
    }
    auto v = vertice[node.id];

    if (value_flow_graph.incoming_nodes_size(node.id) > 0) {
      auto &incoming_nodes = value_flow_graph.incoming_nodes(node.id);

      for (auto &neighbor_iter : incoming_nodes) {
        auto &incoming_node = value_flow_graph.node(neighbor_iter.second);

        if (vertice.find(incoming_node.id) == vertice.end()) {
          auto type = get_value_flow_node_type(incoming_node.type);
          auto redundancy = 0.0;
          if (hot_apis.find(incoming_node.id) != hot_apis.end()) {
            redundancy = hot_apis.at(incoming_node.id).first;
          }
          auto v = boost::add_vertex(VertexProperty(incoming_node.id, type, redundancy), g);
          vertice[incoming_node.id] = v;
        }
        auto iv = vertice[incoming_node.id];
        auto type = get_value_flow_edge_type(neighbor_iter.first);
        boost::add_edge(iv, v, EdgeProperty(type), g);
      }
    }
  }

  boost::dynamic_properties dp;
  dp.property("node_id", boost::get(&VertexProperty::node_id, g));
  dp.property("overwrite", boost::get(&VertexProperty::overwrite, g));
  dp.property("redundancy", boost::get(&VertexProperty::redundancy, g));
  dp.property("type", boost::get(&EdgeProperty::type, g));

  std::ofstream out("value_flow.csv");
  boost::write_graphviz_dp(out, g, dp);
}

bool report_value_flow(const ValueFlowGraph &value_flow_graph,
                       const std::vector<ValueFlowOp> &value_flow_ops) {
  std::map<int32_t, std::map<int32_t, bool>> duplicate;
  analyze_duplicate(value_flow_ops, duplicate);

  std::map<int32_t, std::pair<double, int>> hot_apis;
  analyze_hot_api(value_flow_ops, hot_apis);

  // analyze_hot_pc

  dump_value_flow(value_flow_graph, duplicate, hot_apis);

  if (DEBUG_VALUE_FLOW) {
    for (auto &value_flow_op : value_flow_ops) {
      std::cout << "op: (" << value_flow_op.id << "," << value_flow_op.type << ")" << std::endl;
      std::cout << "redundancy: " << value_flow_op.redundancy << std::endl;
    }
    std::cout << std::endl;

    for (auto node_iter = value_flow_graph.nodes_begin(); node_iter != value_flow_graph.nodes_end();
         ++node_iter) {
      auto node_id = node_iter->first;
      auto &node = node_iter->second;
      std::cout << "node: (" << node_id << ", " << node.type << ")" << std::endl;
      std::cout << "edge: ";
      if (value_flow_graph.incoming_nodes_size(node_id) > 0) {
        auto &incoming_nodes = value_flow_graph.incoming_nodes(node_id);

        for (auto &neighbor_iter : incoming_nodes) {
          std::cout << neighbor_iter.second << ",";
        }
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;

    std::cout << "hot nodes: ";
    for (auto &iter : hot_apis) {
      auto &node_id = iter.first;
      auto &api = iter.second;
      std::cout << "(" << node_id << "," << api.first << "),";
    }
    std::cout << std::endl;
  }

  return true;
}

/*
void report_value_flow(const std::map<int32_t, ValueFlowRecord> &value_flow_records) {
  std::ofstream out("value_flow.csv", std::ios::app);
  for (auto &iter : value_flow_records) {
    auto node_id = iter.first;
    auto &record = iter.second;
    out << "node," << node_id << std::endl;
    out << "duplicate node,level" << std::endl;
    for (auto &dup_iter : record.duplicate) {
      auto dup_node_id = dup_iter.first;
      auto total = dup_iter.second ? "total" : "partial";
      out << std::to_string(dup_node_id) << "," << total << std::endl;
    }
    out << "trace node,edges" << std::endl;
    for (auto &trace_iter : record.backtrace) {
      auto trace_node_id = trace_iter.first;
      out << std::to_string(trace_node_id);
      for (auto &neighbor : trace_iter.second) {
        out << "," << std::to_string(neighbor);
      }
      out << std::endl;
    }
  }
}
*/

}  // namespace redshow
