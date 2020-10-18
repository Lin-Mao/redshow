#include "value_flow.h"

#include <map>
#include <queue>
#include <fstream>
#include <iostream>
#include <set>
#include <string>

#include <boost/graph/graphviz.hpp>

#include "redshow_graphviz.h"
#include "common/hash.h"
#include "common/utils.h"

#define DEBUG_VALUE_FLOW 1

namespace redshow {

std::string get_value_flow_edge_type(ValueFlowEdgeType type) {
  static std::string value_flow_edge_type[] = {
    "WRITE", "READ"
  };
  return value_flow_edge_type[type];
}

void ValueFlow::op_callback(std::shared_ptr<Operation> op) {
  // Add a calling context node
  _graph.lock();
  
  update_op_node(op->op_id, op->ctx_id);

  _graph.unlock();
}

void ValueFlow::analysis_begin(u32 cpu_thread, i32 kernel_id) {
  *trace = &(_kernel_trace.at(cpu_thread).at(kernel_id));
}

void ValueFlow::analysis_end(u32 cpu_thread, i32 kernel_id) {
  // Hold this lock during analysis
  _graph.lock();

  // Add a calling context node
  if (!_graph.has_node(kernel_id)) {
    _graph.add_node(kernel_id, kernel_id, redshow::VALUE_FLOW_NODE_KERNEL);
  }

  for (auto memory_op_id : _trace->read_memory_op_ids) {
    if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
      auto node_id = _op_nodes.at(memory_op_id);
      // Link a pure read edge between two calling contexts
      EdgeIndex edge_index = EdgeIndex(node_id, kernel_id, redshow::VALUE_FLOW_EDGE_READ);
      _graph.add_edge(std::move(edge_index), redshow::VALUE_FLOW_EDGE_READ);
    }
  }

  for (auto memory_op_id : _trace->write_memory_op_ids) {
    if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
      auto node_id = _op_nodes.at(memory_op_id);
      EdgeIndex edge_index = EdgeIndex(node_id, kernel_id, redshow::VALUE_FLOW_EDGE_READ);
      _graph.remove_edge(std::make_pair(node_id, kernel_id), redshow::VALUE_FLOW_EDGE_READ);
      // Point the operation to the calling context
      update_op_node(memory_op_id, kernel_id);
    }
  }

  _graph.unlock();

  _trace->read_memory_op_id.clear();
  _trace->write_memory_op_id.clear();
  _trace = NULL;
}

void ValueFlow::block_enter(const ThreadId &thread_id) {
  // No operation
}

void ValueFlow::block_exit(const ThreadId &thread_id) {
  // No operation
}

void ValueFlow::unit_access(const ThreadId &thread_id, const AccessKind &access_kind,
                            u64 memory_op_id, u64 pc, u64 value, u64 addr, u32 stride, u32 index,
                            bool read) {
  if (read) {
    _trace->read_memory_op_ids.insert(memory_op_id);
  } else {
    _trace->write_memory_op_ids.insert(memory_op_id);
  }
}

void flush_thread(u32 cpu_thread, const std::string &output_dir, const Map<u32, Cubin> &cubins,
                  redshow_record_data_callback_func *record_data_callback) {}

void ValueFlow::flush(const std::string &output_dir, const Map<u32, Cubin> &cubins,
                      const std::vector<OperationPtr> &operations,
                      redshow_record_data_callback_func *record_data_callback) {
  Map<i32, Map<i32, bool>> duplicate;
  analyze_duplicate(operations, duplicate);

  Map<i32, std::pair<double, int>> hot_apis;
  analyze_hot_api(operations, hot_apis);

  // analyze_hot_pc

  dump_value_flow(output_dir, _graph, duplicate, hot_apis);

  if (DEBUG_VALUE_FLOW) {
    for (auto &value_flow_op : operations) {
      std::cout << "op: (" << value_flow_op.id << "," << value_flow_op.type << ")" << std::endl;
      std::cout << "redundancy: " << value_flow_op.redundancy << std::endl;
    }
    std::cout << std::endl;

    for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end();
         ++node_iter) {
      auto node_id = node_iter->first;
      auto &node = node_iter->second;
      std::cout << "node: (" << node_id << ", " << node.type << ")" << std::endl;
      std::cout << "edge: ";
      if (_graph.incoming_nodes_size(node_id) > 0) {
        auto &incoming_nodes = _graph.incoming_nodes(node_id);

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
}

void ValueFlow::update_op_node(i64 op_id, i32 ctx_id) {
  if (!_graph.has_node(op->ctx_id)) {
    // Allocate calling context node
    _graph.add_node(op->ctx_id, op->ctx_id, op->type);
  }

  // Point the operation to the calling context
  if (_op_nodes.find(op->op_id) != _op_nodes.end()) {
    auto prev_ctx_id = _op_nodes.at(op->op_id);
    _graph.add_edge(prev_ctx_id, op->ctx_id, VALUE_FLOW_EDGE_ORDER);
  }

  _op_nodes[op_id] = op->ctx_id;
}

void ValueFlow::analyze_duplicate(const Vector<OperationPtr> &operations,
                                  Map<i32, Map<i32, bool>> &duplicate) {
  Map<ui32, std::set<std::string>> node_hashes;
  for (auto &op : operations) {
    node_hashes[op.id].insert(op.hash);
  }

  Map<std::string, Map<i32, bool>> hash_nodes;
  for (auto &op : operations) {
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

void ValueFlow::analyze_hot_api(const std::vector<std::shared_ptr<Operation>> &ops,
                                Map<i32, std::pair<double, int>> &hot_apis) {
  for (auto &op : operations) {
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

// TODO(Keren): a template dump pattern
void ValueFlow::dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate,
                     const Map<i32, std::pair<double, int>> &hot_apis) {
  typedef redshow_graphviz_node VertexProperty;
  typedef redshow_graphviz_edge EdgeProperty;
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, VertexProperty,
                                EdgeProperty> Graph;
  typedef boost::graph_traits<Graph>::vertex_descriptor vertex_descriptor;
  typedef boost::graph_traits<Graph>::edge_descriptor edge_descriptor;
  Graph g;
  Map<i32, vertex_descriptor> vertice;
  for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end();
       ++node_iter) {
    auto &node = _graph.node(node_iter->first);
    if (vertice.find(node.id) == vertice.end()) {
      auto type = get_operation_node_type(node.type);
      auto redundancy = 0.0;
      if (hot_apis.find(node.id) != hot_apis.end()) {
        redundancy = hot_apis.at(node.id).first;
      }
      auto v = boost::add_vertex(VertexProperty(node.id, type, redundancy), g);
      vertice[node.id] = v;
    }
    auto v = vertice[node.id];

    if (_graph.incoming_nodes_size(node.id) > 0) {
      auto &incoming_nodes = _graph.incoming_nodes(node.id);

      for (auto &neighbor_iter : incoming_nodes) {
        auto &incoming_node = _graph.node(neighbor_iter.second);

        if (vertice.find(incoming_node.id) == vertice.end()) {
          auto type = get_operation_node_type(incoming_node.type);
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

  std::ofstream out(output_dir + "value_flow.dot");
  boost::write_graphviz_dp(out, g, dp);
}

}  // namespace redshow
