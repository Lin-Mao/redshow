#include "analysis/data_flow.h"

#include <boost/graph/graphviz.hpp>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>

#include "common/hash.h"
#include "common/utils.h"
#include "operation/memcpy.h"
#include "operation/memset.h"
#include "redshow_graphviz.h"

#define DEBUG_DATA_FLOW 0

namespace redshow {

std::string DataFlow::get_data_flow_edge_type(EdgeType type) {
  static std::string data_flow_edge_type[] = {"WRITE", "READ"};
  return data_flow_edge_type[type];
}

void DataFlow::init() {
  // A special context id for untrackable memorys
  _op_node[REDSHOW_MEMORY_SHARED] = SHARED_MEMORY_CTX_ID;
  _op_node[REDSHOW_MEMORY_LOCAL] = LOCAL_MEMORY_CTX_ID;
  _op_node[REDSHOW_MEMORY_CONSTANT] = CONSTANT_MEMORY_CTX_ID;
  _op_node[REDSHOW_MEMORY_UVM] = UVM_MEMORY_CTX_ID;
  _op_node[REDSHOW_MEMORY_HOST] = HOST_MEMORY_CTX_ID;

  _graph.add_node(SHARED_MEMORY_CTX_ID, SHARED_MEMORY_CTX_ID, OPERATION_TYPE_MEMORY);
  _graph.add_node(CONSTANT_MEMORY_CTX_ID, CONSTANT_MEMORY_CTX_ID, OPERATION_TYPE_MEMORY);
  _graph.add_node(UVM_MEMORY_CTX_ID, UVM_MEMORY_CTX_ID, OPERATION_TYPE_MEMORY);
  _graph.add_node(HOST_MEMORY_CTX_ID, HOST_MEMORY_CTX_ID, OPERATION_TYPE_MEMORY);
  _graph.add_node(LOCAL_MEMORY_CTX_ID, LOCAL_MEMORY_CTX_ID, OPERATION_TYPE_MEMORY);

  _memories[REDSHOW_MEMORY_SHARED] = std::make_shared<Memory>(REDSHOW_MEMORY_SHARED, SHARED_MEMORY_CTX_ID);
  _memories[REDSHOW_MEMORY_LOCAL] = std::make_shared<Memory>(REDSHOW_MEMORY_LOCAL, LOCAL_MEMORY_CTX_ID);
  _memories[REDSHOW_MEMORY_CONSTANT] = std::make_shared<Memory>(REDSHOW_MEMORY_CONSTANT, CONSTANT_MEMORY_CTX_ID);
  _memories[REDSHOW_MEMORY_UVM] = std::make_shared<Memory>(REDSHOW_MEMORY_UVM, UVM_MEMORY_CTX_ID);
  _memories[REDSHOW_MEMORY_HOST] = std::make_shared<Memory>(REDSHOW_MEMORY_HOST, HOST_MEMORY_CTX_ID);
}

void DataFlow::kernel_op_callback(std::shared_ptr<Kernel> op) {
  // data flow analysis must be synchrounous
  for (auto &mem_iter : _trace->read_memory) {
    // Avoid local and share memories
    auto memory = _memories.at(mem_iter.first);
    auto node_id = _op_node.at(memory->op_id);
    // Link a pure read edge between two calling contexts
    link_ctx_node(node_id, op->ctx_id, memory->ctx_id, DATA_FLOW_EDGE_READ);
  }

  for (auto &mem_iter : _trace->write_memory) {
    auto memory = _memories.at(mem_iter.first);
    if (memory->op_id > REDSHOW_MEMORY_HOST) {
      auto len = 0;
      #pragma unroll 8
      for (size_t i = 0; i < mem_iter.second.size(); ++i) {
        len++;
      }
      u64 host = reinterpret_cast<u64>(memory->value.get());
      u64 host_cache = reinterpret_cast<u64>(memory->value_cache.get());
      u64 device = memory->memory_range.start;
      dtoh(host_cache, device, memory->len);
      u64 redundancy = compute_memcpy_redundancy(host_cache, host, memory->len);
      u64 overwrite = len;

      // Point the operation to the calling context
      link_op_node(memory->op_id, op->ctx_id, memory->ctx_id);
      update_op_metrics(memory->op_id, op->ctx_id, memory->ctx_id, redundancy, overwrite, memory->len);
      update_op_node(memory->op_id, op->ctx_id);

      std::string hash = compute_memory_hash(reinterpret_cast<u64>(host_cache), memory->len);
      _node_hash[op->ctx_id].emplace(hash);

      // XXX(Keren): This is tricky, should let the tool do update
      memcpy(reinterpret_cast<void *>(host), reinterpret_cast<void *>(host_cache), memory->len);
    }
  }

  _trace->read_memory.clear();
  _trace->write_memory.clear();
  _trace = NULL;
}

void DataFlow::memory_op_callback(std::shared_ptr<Memory> op) {
  update_op_node(op->op_id, op->ctx_id);
  _memories.try_emplace(op->op_id, op);
}

void DataFlow::memset_op_callback(std::shared_ptr<Memset> op) {
  u64 redundancy = compute_memset_redundancy(op->shadow_start, op->value, op->shadow_len);
  u64 overwrite = op->len;

  auto memory = _memories.at(op->memory_op_id);
  link_op_node(op->memory_op_id, op->ctx_id, memory->ctx_id);
  update_op_metrics(op->memory_op_id, op->ctx_id, memory->ctx_id, redundancy, overwrite, op->shadow_len);
  update_op_node(op->memory_op_id, op->ctx_id);

  u64 host = reinterpret_cast<u64>(memory->value_cache.get());
  dtoh(host, memory->memory_range.start, memory->len);
  std::string hash = compute_memory_hash(host, op->shadow_len);
  _node_hash[op->ctx_id].emplace(hash);
}

void DataFlow::memcpy_op_callback(std::shared_ptr<Memcpy> op) {
  u64 redundancy = compute_memcpy_redundancy(op->dst_start, op->src_start, op->len);
  u64 overwrite = op->len;
  u64 dst_len = op->dst_len == 0 ? op->len : op->dst_len;

  auto src_memory = _memories.at(op->src_memory_op_id);
  auto dst_memory = _memories.at(op->dst_memory_op_id);
  link_op_node(op->dst_memory_op_id, op->ctx_id, dst_memory->ctx_id);
  update_op_metrics(op->dst_memory_op_id, op->ctx_id, dst_memory->ctx_id, redundancy, overwrite, dst_len);
  update_op_node(op->dst_memory_op_id, op->ctx_id);

  auto src_ctx_id = _op_node.at(op->src_memory_op_id);
  auto dst_ctx_id = _op_node.at(op->dst_memory_op_id);
  link_ctx_node(src_ctx_id, dst_ctx_id, src_memory->ctx_id, DATA_FLOW_EDGE_READ);

  std::string hash = compute_memory_hash(op->src_start, op->len);
  _node_hash[op->ctx_id].emplace(hash);
}

void DataFlow::op_callback(OperationPtr op) {
  // Add a calling context node
  lock();

  if (!_graph.has_node(op->ctx_id)) {
    // Allocate calling context node
    _graph.add_node(std::move(op->ctx_id), op->ctx_id, op->type);
  }

  if (op->type == OPERATION_TYPE_KERNEL) {
    kernel_op_callback(std::dynamic_pointer_cast<Kernel>(op));
  } else if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op));
  } else if (op->type == OPERATION_TYPE_MEMCPY) {
    memcpy_op_callback(std::dynamic_pointer_cast<Memcpy>(op));
  } else if (op->type == OPERATION_TYPE_MEMSET) {
    memset_op_callback(std::dynamic_pointer_cast<Memset>(op));
  }

  unlock();
}

void DataFlow::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id) {
  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<DataFlowTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<DataFlowTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void DataFlow::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void DataFlow::block_enter(const ThreadId &thread_id) {
  // No operation
}

void DataFlow::block_exit(const ThreadId &thread_id) {
  // No operation
}

void DataFlow::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           bool read) {
  if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  auto array_size = memory.memory_range.end - memory.memory_range.start;
  auto byte_size = access_kind.unit_size >> 3;
  auto offset = addr - memory.memory_range.start;
  if (read) {
    _trace->read_memory[memory.op_id].resize(array_size);
    #pragma unroll 4
    for (size_t i = 0; i < byte_size; ++i) {
      _trace->read_memory[memory.op_id][offset + i] = true;
    }
  } else {
    _trace->write_memory[memory.op_id].resize(array_size);
    #pragma unroll 4
    for (size_t i = 0; i < byte_size; ++i) {
      _trace->write_memory[memory.op_id][offset + i] = true;
    }
  }
}

void DataFlow::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void DataFlow::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {
  Map<i32, Map<i32, bool>> duplicate;
  analyze_duplicate(duplicate);

  dump(output_dir, duplicate);

#ifdef DEBUG_DATA_FLOW
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
#endif
}

void DataFlow::link_ctx_node(i32 src_ctx_id, i32 dst_ctx_id, i32 mem_ctx_id, EdgeType type) {
  auto edge_index = EdgeIndex(src_ctx_id, dst_ctx_id, mem_ctx_id, type);
  _graph.add_edge(std::move(edge_index), type);
}

void DataFlow::link_op_node(u64 op_id, i32 ctx_id, i32 mem_ctx_id) {
  if (_op_node.find(op_id) != _op_node.end()) {
    auto prev_ctx_id = _op_node.at(op_id);
    link_ctx_node(prev_ctx_id, ctx_id, mem_ctx_id, DATA_FLOW_EDGE_ORDER);
  }
}

void DataFlow::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}

void DataFlow::update_op_metrics(u64 op_id, i32 ctx_id, i32 mem_ctx_id, u64 redundancy, u64 overwrite, u64 count) {
  // Update current edge's property
  if (_op_node.find(op_id) != _op_node.end()) {
    auto prev_ctx_id = _op_node.at(op_id);
    auto edge_index = EdgeIndex(prev_ctx_id, ctx_id, mem_ctx_id, DATA_FLOW_EDGE_ORDER);
    if (_graph.has_edge(edge_index)) {
      auto &edge = _graph.edge(edge_index);
      edge.redundancy += redundancy;
      edge.overwrite += overwrite;
      edge.count += count;
    }
  }
}

void DataFlow::analyze_duplicate(Map<i32, Map<i32, bool>> &duplicate) {
  Map<std::string, Map<i32, bool>> hash_nodes;
  for (auto &node_iter : _node_hash) {
    auto node_id = node_iter.first;
    if (node_iter.second.size() > 1) {
      // Partial duplicate
      for (auto &hash : node_iter.second) {
        hash_nodes[hash][node_id] = false;
      }
    } else {
      // Total duplicate
      for (auto &hash : node_iter.second) {
        hash_nodes[hash][node_id] = true;
      }
    }
  }

  // Construct duplicate connections
  for (auto &iter : _node_hash) {
    auto node_id = iter.first;
    for (auto &hash : iter.second) {
      for (auto &node_iter : hash_nodes[hash]) {
        auto dup_node_id = node_iter.first;
        if (dup_node_id != node_id) {
          auto total = node_iter.second && iter.second.size() == 1;
          duplicate[node_id][dup_node_id] = total;
          duplicate[dup_node_id][node_id] = total;
        }
      }
    }
  }
}

// TODO(Keren): a template dump pattern
void DataFlow::dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate) {
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
      std::string type;
      if (node.ctx_id == SHARED_MEMORY_CTX_ID) {
        type = "SHARED";
      } else if (node.ctx_id == CONSTANT_MEMORY_CTX_ID) {
        type = "CONSTANT";
      } else if (node.ctx_id == UVM_MEMORY_CTX_ID) {
        type = "UVM";
      } else if (node.ctx_id == HOST_MEMORY_CTX_ID) {
        type = "HOST";
      } else if (node.ctx_id == LOCAL_MEMORY_CTX_ID) {
        type = "LOCAL";
      } else {
        type = get_operation_type(node.type);
      }

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
        auto edge_count = edge.count == 0 ? 1 : edge.count;
        auto redundancy_avg = edge.redundancy / static_cast<double>(edge_count);
        auto overwrite_avg = edge.overwrite / static_cast<double>(edge_count);

        auto iv = vertice[incoming_node.ctx_id];
        auto type = get_data_flow_edge_type(edge_index.type);
        boost::add_edge(iv, v, EdgeProperty(type, edge_index.mem_ctx_id, redundancy_avg, overwrite_avg), g);
      }
    }
  }

  boost::dynamic_properties dp;
  dp.property("node_id", boost::get(&VertexProperty::node_id, g));
  dp.property("node_type", boost::get(&VertexProperty::type, g));
  dp.property("duplicate", boost::get(&VertexProperty::duplicate, g));
  dp.property("edge_type", boost::get(&EdgeProperty::type, g));
  dp.property("memory_node_id", boost::get(&EdgeProperty::memory_node_id, g));
  dp.property("overwrite", boost::get(&EdgeProperty::overwrite, g));
  dp.property("redundancy", boost::get(&EdgeProperty::redundancy, g));

  std::ofstream out(output_dir + "data_flow.dot");
  boost::write_graphviz_dp(out, g, dp);
}

}  // namespace redshow
