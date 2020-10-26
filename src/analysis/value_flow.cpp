#include "analysis/value_flow.h"

#include <boost/graph/graphviz.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string>

#include "common/hash.h"
#include "common/utils.h"
#include "operation/memcpy.h"
#include "operation/memset.h"
#include "redshow_graphviz.h"

#define DEBUG_VALUE_FLOW 1

namespace redshow {

std::string ValueFlow::get_value_flow_edge_type(EdgeType type) {
  static std::string value_flow_edge_type[] = {"WRITE", "READ"};
  return value_flow_edge_type[type];
}

void ValueFlow::memory_op_callback(std::shared_ptr<Memory> op) {
  update_op_node(op->op_id, op->ctx_id);
}

void ValueFlow::memset_op_callback(std::shared_ptr<Memset> op) {
  double redundancy = compute_memset_redundancy(op->shadow_start, op->value, op->len);

  double overwrite = 0.0;
  overwrite = op->len / op->shadow_len;

  link_op_node(op->memory_op_id, op->ctx_id);
  update_op_metrics(op->memory_op_id, op->ctx_id, redundancy, overwrite);
  update_op_node(op->memory_op_id, op->ctx_id);

  // XXX(Keren): can be improved, op->shadow only has old result
  u8 new_shadow[op->shadow_len];
  memcpy(reinterpret_cast<void *>(new_shadow), reinterpret_cast<void *>(op->shadow_start),
         op->shadow_len);
  memset(reinterpret_cast<void *>(new_shadow), op->value, op->len);
  std::string hash = compute_memory_hash(reinterpret_cast<u64>(new_shadow), op->shadow_len);
  _node_hash[op->ctx_id].emplace(hash);
}

void ValueFlow::memcpy_op_callback(std::shared_ptr<Memcpy> op) {
  double redundancy = compute_memset_redundancy(op->dst_start, op->src_start, op->len);

  double overwrite = 0.0;
  overwrite = op->len / op->dst_len;

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    link_op_node(op->dst_memory_op_id, op->ctx_id);
    update_op_metrics(op->dst_memory_op_id, op->ctx_id, redundancy, overwrite);
  }

  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {
    auto src_ctx_id = _op_node.at(op->src_memory_op_id);
    auto dst_ctx_id = _op_node.at(op->dst_memory_op_id);
    link_ctx_node(src_ctx_id, dst_ctx_id, VALUE_FLOW_EDGE_READ);
  }

  update_op_node(op->dst_memory_op_id, op->ctx_id);

  std::string hash = compute_memory_hash(op->dst_start, op->dst_len);
  _node_hash[op->ctx_id].emplace(hash);
}

void ValueFlow::op_callback(OperationPtr op) {
  // Add a calling context node
  lock();

  if (!_graph.has_node(op->ctx_id)) {
    // Allocate calling context node
    _graph.add_node(std::move(op->ctx_id), op->ctx_id, op->type);
  }

  if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op));
  } else if (op->type == OPERATION_TYPE_MEMCPY) {
    memcpy_op_callback(std::dynamic_pointer_cast<Memcpy>(op));
  } else if (op->type == OPERATION_TYPE_MEMSET) {
    memset_op_callback(std::dynamic_pointer_cast<Memset>(op));
  }

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
      link_ctx_node(node_id, kernel_id, VALUE_FLOW_EDGE_READ);
    }
  }

  for (auto memory_op_id : _trace->write_memory_op_ids) {
    if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
      // Point the operation to the calling context
      update_op_node(memory_op_id, kernel_id);
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
                      redshow_record_data_callback_func record_data_callback) {
  Map<i32, Map<i32, bool>> duplicate;
  analyze_duplicate(duplicate);

  // analyze_hot_pc

  dump(output_dir, duplicate);

  if (DEBUG_VALUE_FLOW) {
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
  }
}

void ValueFlow::link_ctx_node(i32 src_ctx_id, i32 dst_ctx_id, EdgeType type) {
  auto edge_index = EdgeIndex(src_ctx_id, dst_ctx_id, type);
  _graph.add_edge(std::move(edge_index), type);
  _graph.edge(edge_index).count += 1;
}

void ValueFlow::link_op_node(u64 op_id, i32 ctx_id) {
  if (_op_node.find(op_id) != _op_node.end()) {
    auto prev_ctx_id = _op_node.at(op_id);
    link_ctx_node(prev_ctx_id, ctx_id, VALUE_FLOW_EDGE_ORDER);
  }
}

void ValueFlow::update_op_node(u64 op_id, i32 ctx_id) {
  // Point the operation to the calling context
  _op_node[op_id] = ctx_id;
}

void ValueFlow::update_op_metrics(u64 op_id, i32 ctx_id, double redundancy, double overwrite) {
  // Update current edge's property
  if (_op_node.find(op_id) != _op_node.end()) {
    auto prev_ctx_id = _op_node.at(op_id);
    auto edge_index = EdgeIndex(prev_ctx_id, ctx_id, VALUE_FLOW_EDGE_ORDER);
    if (_graph.has_edge(edge_index)) {
      auto &edge = _graph.edge(edge_index);
      edge.redundancy += redundancy;
      edge.overwrite += overwrite;
    }
  }
}

void ValueFlow::analyze_duplicate(Map<i32, Map<i32, bool>> &duplicate) {
  Map<std::string, Map<i32, bool>> hash_nodes;
  for (auto &node_iter : _node_hash) {
    auto node_id = node_iter.first;
    std::optional<std::string> hash;
    if (node_iter.second.size() > 1) {
        // Partial duplicate
      hash_nodes[hash.value()][node_id] = false;
    } else {
      // Total duplicate
      hash_nodes[hash.value()][node_id] = true;
    }
  }

  // Construct duplicate connections
  for (auto &iter : _node_hash) {
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

// TODO(Keren): a template dump pattern
void ValueFlow::dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate) {
  typedef redshow_graphviz_node VertexProperty;
  typedef redshow_graphviz_edge EdgeProperty;
  typedef boost::adjacency_list<boost::vecS, boost::vecS, boost::directedS, VertexProperty,
                                EdgeProperty>
      Graph;
  typedef boost::graph_traits<Graph>::vertex_descriptor vertex_descriptor;
  typedef boost::graph_traits<Graph>::edge_descriptor edge_descriptor;
  Graph g;
  Map<i32, vertex_descriptor> vertice;
  for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end(); ++node_iter) {
    auto &node = node_iter->second;
    if (vertice.find(node.ctx_id) == vertice.end()) {
      auto type = get_operation_type(node.type);
      auto redundancy = 0.0;
      std::string dup;
      if (duplicate.has(node.ctx_id)) {
        for (auto &iter : duplicate.at(node.ctx_id)) {
          auto total = iter.second ? "TOTAL" : "PARTIAL";
          dup += std::to_string(iter.first) + "," + total + ";";
        }
      }
      auto v = boost::add_vertex(VertexProperty(node.ctx_id, type, dup), g);
      vertice[node.ctx_id] = v;
    }
  }

  for (auto node_iter = _graph.nodes_begin(); node_iter != _graph.nodes_end(); ++node_iter) {
    auto &node = node_iter->second;
    auto v = vertice[node.ctx_id];

    if (_graph.incoming_edge_size(node.ctx_id) > 0) {
      auto &incoming_edges = _graph.incoming_edges(node.ctx_id);

      for (auto &edge_index : incoming_edges) {
        auto &incoming_node = _graph.node(edge_index.from);
        auto &edge = _graph.edge(edge_index);
        auto redundancy_avg = edge.redundancy / edge.count;
        auto overwrite_avg = edge.overwrite / edge.overwrite;

        auto iv = vertice[incoming_node.ctx_id];
        auto type = get_value_flow_edge_type(edge_index.type);
        boost::add_edge(iv, v, EdgeProperty(type, redundancy_avg, overwrite_avg), g);
      }
    }
  }

  boost::dynamic_properties dp;
  dp.property("node_id", boost::get(&VertexProperty::node_id, g));
  dp.property("node_type", boost::get(&VertexProperty::type, g));
  dp.property("duplicate", boost::get(&VertexProperty::duplicate, g));
  dp.property("edge_type", boost::get(&EdgeProperty::type, g));
  dp.property("overwrite", boost::get(&EdgeProperty::overwrite, g));
  dp.property("redundancy", boost::get(&EdgeProperty::redundancy, g));

  std::ofstream out(output_dir + "value_flow.dot");
  boost::write_graphviz_dp(out, g, dp);
}

}  // namespace redshow
