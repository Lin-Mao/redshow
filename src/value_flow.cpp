#include "value_flow.h"

#include <map>
#include <queue>
#include <set>
#include <string>

#include "hash.h"
#include "utils.h"

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

static void analyze_duplicate(const std::vector<ValueFlowOp> &value_flow_ops,
                              std::map<int32_t, ValueFlowRecord> &value_flow_records) {
  std::map<uint32_t, std::set<std::string>> node_hashes;
  for (auto &op : value_flow_ops) {
    node_hashes[op.id].emplace(op.hash);
  }

  std::map<std::string, std::map<int32_t, bool>> hash_nodes;
  for (auto &op : value_flow_ops) {
    if (node_hashes[op.id].size() > 1) {
      // Partial duplicate
      hash_nodes[op.hash].emplace(std::make_pair(op.id, false));
    } else {
      // Total duplicate
      hash_nodes[op.hash].emplace(std::make_pair(op.id, true));
    }
  }

  // Construct duplicate connections
  for (auto &iter : node_hashes) {
    auto node_id = iter.first;
    for (auto &hash : iter.second) {
      for (auto &node_iter : hash_nodes[hash]) {
        auto dup_node_id = node_iter.first;
        auto total = node_iter.second;
        value_flow_records[node_id].id = node_id;
        value_flow_records[node_id].duplicate.emplace(std::make_pair(dup_node_id, total));
      }
    }
  }
}

static void analyze_hot_api(const std::vector<ValueFlowOp> &value_flow_ops,
                            std::map<int32_t, std::pair<double, int>> &hot_apis) {
  for (auto &op : value_flow_ops) {
    if (op.redundancy > 0) {
      auto redundancy_count = hot_apis[op.id];
      redundancy_count.first += op.redundancy;
      redundancy_count.second += 1;
      hot_apis.emplace(std::make_pair(op.id, redundancy_count));
    }
  }

  // Calculate average
  for (auto &iter : hot_apis) {
    auto &api = iter.second;
    api.first = api.first / api.second;
  }
}

static void backprop_bfs(const ValueFlowGraph &value_flow_graph,
                         const std::map<int32_t, std::pair<double, int>> &hot_apis, int32_t node_id,
                         std::map<int32_t, std::set<int32_t>> &trace_graph) {
  const int BACKPROP_RED = 0.9;

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

        if (edge_type == VALUE_FLOW_EDGE_READ || hot_apis.at(out_id).first > BACKPROP_RED) {
          if (trace_graph.find(curr_id) == trace_graph.end()) {
            queue.push(curr_id);
          }
          trace_graph[curr_id].emplace(out_id);
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

bool analyze_value_flow(const ValueFlowGraph &value_flow_graph,
                        const std::vector<ValueFlowOp> &value_flow_ops,
                        std::map<int32_t, ValueFlowRecord> &value_flow_records) {
  analyze_duplicate(value_flow_ops, value_flow_records);

  std::map<int32_t, std::pair<double, int>> hot_apis;
  analyze_hot_api(value_flow_ops, hot_apis);

  // analyze_hot_pc

  backprop(value_flow_graph, hot_apis, value_flow_records);

  return true;
}

void report_value_flow(const std::map<int32_t, ValueFlowRecord> &value_flow_records) {
}

}  // namespace redshow
