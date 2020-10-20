#include "analysis/value_flow.h"

#include <map>
#include <queue>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>

#include <boost/graph/graphviz.hpp>

#include "redshow_graphviz.h"
#include "common/hash.h"
#include "common/utils.h"
#include "operation/memcpy.h"
#include "operation/memset.h"

#define DEBUG_VALUE_FLOW 1

namespace redshow {

std::string ValueFlow::get_value_flow_edge_type(EdgeType type) {
  static std::string value_flow_edge_type[] = {
    "WRITE", "READ"
  };
  return value_flow_edge_type[type];
}

void ValueFlow::op_callback(OperationPtr op) {
  // Add a calling context node
  lock();

  update_op_node(op);

  unlock();
}

void ValueFlow::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id) {
  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<ValueFlowTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<ValueFlowTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void ValueFlow::analysis_end(u32 cpu_thread, i32 kernel_id) {
  lock();

  // Add a calling context node
  if (!_graph.has_node(kernel_id)) {
    _graph.add_node(std::move(kernel_id), kernel_id, OPERATION_TYPE_KERNEL);
  }

  for (auto memory_op_id : _trace->read_memory_op_ids) {
    if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
      auto node_id = _op_node.at(memory_op_id);
      // Link a pure read edge between two calling contexts
      EdgeIndex edge_index = EdgeIndex(node_id, kernel_id, VALUE_FLOW_EDGE_READ);
      _graph.add_edge(std::move(edge_index), VALUE_FLOW_EDGE_READ);
    }
  }

  for (auto memory_op_id : _trace->write_memory_op_ids) {
    if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
      // Point the operation to the calling context
      OperationPtr op = std::make_shared<Kernel>(memory_op_id, kernel_id);
      update_op_node(op);
    }
  }

  unlock();

  _trace->read_memory_op_ids.clear();
  _trace->write_memory_op_ids.clear();
  _trace = NULL;
}

void ValueFlow::block_enter(const ThreadId &thread_id) {
  // No operation
}

void ValueFlow::block_exit(const ThreadId &thread_id) {
  // No operation
}

void ValueFlow::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                            u64 memory_op_id, u64 pc, u64 value, u64 addr, u32 stride, u32 index,
                            bool read) {
  if (read) {
    _trace->read_memory_op_ids.insert(memory_op_id);
  } else {
    _trace->write_memory_op_ids.insert(memory_op_id);
  }
}

void ValueFlow::flush_thread(u32 cpu_thread, const std::string &output_dir,
                             const LockableMap<u32, Cubin> &cubins,
                             redshow_record_data_callback_func record_data_callback) {}

void ValueFlow::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                      const Vector<OperationPtr> &ops,
                      redshow_record_data_callback_func record_data_callback) {
  Map<i32, Map<i32, bool>> duplicate;
  analyze_duplicate(ops, duplicate);

  Map<i32, std::pair<double, int>> hot_apis;
  analyze_hot_api(ops, hot_apis);

  Map<i32, std::pair<double, int>> overwrite_rate;
  analyze_overwrite(ops, overwrite_rate);

  // analyze_hot_pc

  dump(output_dir, duplicate, hot_apis, overwrite_rate);

  if (DEBUG_VALUE_FLOW) {
    for (auto &op : ops) {
      std::cout << "op: (" << op->ctx_id << ", " << op->op_id << ", " << get_operation_type(op->type)
                << ")" << std::endl;

      auto redundancy = 0.0;
      if (op->type == OPERATION_TYPE_MEMCPY) {
        auto op_memcpy = std::dynamic_pointer_cast<Memcpy>(op);
        redundancy = op_memcpy->redundancy;
      } else if (op->type == OPERATION_TYPE_MEMSET) {
        auto op_memset = std::dynamic_pointer_cast<Memset>(op);
        redundancy = op_memset->redundancy;
      }
      std::cout << "redundancy: " << redundancy << std::endl;
    }
    std::cout << std::endl;

    for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end(); ++node_iter) {
      auto node_id = node_iter->first;
      auto &node = node_iter->second;
      std::cout << "node: (" << node_id << ", " << node.type << ")" << std::endl;
      std::cout << "edge: ";
      if (_graph.incoming_edge_size(node_id) > 0) {
        auto &incoming_edges = _graph.incoming_edges(node_id);

        for (auto &edge_index : incoming_edges) {
          std::cout << edge_index.to << ",";
        }
      }
      std::cout << std::endl;
    }
    std::cout << std::endl;

    std::cout << "hot nodes: ";
    for (auto &iter : hot_apis) {
      auto &node_id = iter.first;
      auto &api = iter.second;
      std::cout << "(" << node_id << ", " << api.first << "),";
    }
    std::cout << std::endl;

    std::cout << "overwrite: ";
    for (auto &iter : overwrite_rate) {
      auto &node_id = iter.first;
      auto &rate = iter.second;
      std::cout << "(" << node_id << ", " << rate.first << "),";
    }
    std::cout << std::endl;
  }
}

void ValueFlow::update_op_node(OperationPtr op) {
  if (!_graph.has_node(op->ctx_id)) {
    // Allocate calling context node
    _graph.add_node(std::move(op->ctx_id), op->ctx_id, op->type);
  }

  // Point the operation to the calling context
  if (_op_node.find(op->op_id) != _op_node.end()) {
    auto prev_ctx_id = _op_node.at(op->op_id);
    auto edge_index = EdgeIndex(prev_ctx_id, op->ctx_id, VALUE_FLOW_EDGE_ORDER);
    _graph.add_edge(std::move(edge_index), VALUE_FLOW_EDGE_ORDER);
  }

  _op_node[op->op_id] = op->ctx_id;
}

void ValueFlow::analyze_duplicate(const Vector<OperationPtr> &ops,
                                  Map<i32, Map<i32, bool>> &duplicate) {
  void analyze_duplicate(const Vector<OperationPtr> &ops,
                         Map<i32, Map<i32, bool>> &duplicate);

  Map<i32, std::set<std::string>> node_hashes;
  for (auto op : ops) {
    std::optional<std::string> hash;
    if (op->type == OPERATION_TYPE_MEMCPY) {
      auto op_memcpy = std::dynamic_pointer_cast<Memcpy>(op);
      hash = op_memcpy->hash;
    } else if (op->type == OPERATION_TYPE_MEMSET) {
      auto op_memset = std::dynamic_pointer_cast<Memset>(op);
      hash = op_memset->hash;
    }
    if (hash.has_value()) {
      node_hashes[op->op_id].emplace(hash.value());
    }
  }

  Map<std::string, Map<i32, bool>> hash_nodes;
  for (auto &op : ops) {
    std::optional<std::string> hash;
    if (op->type == OPERATION_TYPE_MEMCPY) {
      auto op_memcpy = std::dynamic_pointer_cast<Memcpy>(op);
      hash = op_memcpy->hash;
    } else if (op->type == OPERATION_TYPE_MEMSET) {
      auto op_memset = std::dynamic_pointer_cast<Memset>(op);
      hash = op_memset->hash;
    }
    if (hash.has_value()) {
      if (node_hashes[op->ctx_id].size() > 1) {
        // Partial duplicate
        hash_nodes[hash.value()][op->ctx_id] = false;
      } else {
        // Total duplicate
        hash_nodes[hash.value()][op->ctx_id] = true;
      }
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

void ValueFlow::analyze_hot_api(const Vector<OperationPtr> &ops,
                                Map<i32, std::pair<double, int>> &hot_apis) {
  for (auto op : ops) {
    auto redundancy = 0.0;
    if (op->type == OPERATION_TYPE_MEMCPY) {
      auto op_memcpy = std::dynamic_pointer_cast<Memcpy>(op);
      redundancy = op_memcpy->redundancy;
    } else if (op->type == OPERATION_TYPE_MEMSET) {
      auto op_memset = std::dynamic_pointer_cast<Memset>(op);
      redundancy = op_memset->redundancy;
    }
    if (redundancy > 0) {
      auto &redundancy_count = hot_apis[op->ctx_id];
      redundancy_count.first += redundancy;
      redundancy_count.second += 1;
    }
  }

  // Calculate average
  for (auto &iter : hot_apis) {
    auto &api = iter.second;
    api.first = api.first / api.second;
  }
}


void ValueFlow::analyze_overwrite(const Vector<OperationPtr> &ops,
                                  Map<i32, std::pair<double, int>> &overwrite_rate) {
  for (auto op : ops) {
    auto overwrite = 0.0;
    if (op->type == OPERATION_TYPE_MEMCPY) {
      auto op_memcpy = std::dynamic_pointer_cast<Memcpy>(op);
      overwrite = op_memcpy->overwrite;
    } else if (op->type == OPERATION_TYPE_MEMSET) {
      auto op_memset = std::dynamic_pointer_cast<Memset>(op);
      overwrite = op_memset->overwrite;
    }
    if (overwrite > 0) {
      auto &write_count = overwrite_rate[op->ctx_id];
      write_count.first += overwrite;
      write_count.second += 1;
    }
  }

  // Calculate average
  for (auto &iter : overwrite_rate) {
    auto &api = iter.second;
    api.first = api.first / api.second;
  }
}

// TODO(Keren): a template dump pattern
void ValueFlow::dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate,
                     const Map<i32, std::pair<double, int>> &hot_apis,
                     const Map<i32, std::pair<double, int>> &overwrite_rate) {
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
    auto &node = node_iter->second;
    if (vertice.find(node.ctx_id) == vertice.end()) {
      auto type = get_operation_type(node.type);
      auto redundancy = 0.0;
      if (hot_apis.find(node.ctx_id) != hot_apis.end()) {
        redundancy = hot_apis.at(node.ctx_id).first;
      }
      auto overwrite = 0.0;
      if (overwrite_rate.find(node.ctx_id) != overwrite_rate.end()) {
        overwrite = overwrite_rate.at(node.ctx_id).first;
      }
      std::string dup;
      if (duplicate.has(node.ctx_id)) {
        for (auto &iter : duplicate.at(node.ctx_id)) {
          auto total = iter.second ? "TOTAL" : "PARTIAL";
          dup += std::to_string(iter.first) + "," + total + ";";
        }
      } 
      auto v = boost::add_vertex(VertexProperty(node.ctx_id, type, dup, redundancy, overwrite), g);
      vertice[node.ctx_id] = v;
    }
  }

  for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end();
       ++node_iter) {
    auto &node = node_iter->second;
    auto v = vertice[node.ctx_id];

    if (_graph.incoming_edge_size(node.ctx_id) > 0) {
      auto &incoming_edges = _graph.incoming_edges(node.ctx_id);

      for (auto &edge_index : incoming_edges) {
        auto &incoming_node = _graph.node(edge_index.to);

        auto iv = vertice[incoming_node.ctx_id];
        auto type = get_value_flow_edge_type(edge_index.type);
        boost::add_edge(iv, v, EdgeProperty(type), g);
      }
    }
  }

  boost::dynamic_properties dp;
  dp.property("node_id", boost::get(&VertexProperty::node_id, g));
  dp.property("node_type", boost::get(&VertexProperty::type, g));
  dp.property("overwrite", boost::get(&VertexProperty::overwrite, g));
  dp.property("redundancy", boost::get(&VertexProperty::redundancy, g));
  dp.property("duplicate", boost::get(&VertexProperty::duplicate, g));
  dp.property("edge_type", boost::get(&EdgeProperty::type, g));

  std::ofstream out(output_dir + "value_flow.dot");
  boost::write_graphviz_dp(out, g, dp);
}

}  // namespace redshow
