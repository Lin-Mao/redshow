#ifndef REDSHOW_VALUE_FLOW_H
#define REDSHOW_VALUE_FLOW_H

#include <map>
#include <set>
#include <string>
#include <vector>

namespace redshow {

/**
 * @brief calculate a hash for the memory region
 *
 * @param start
 * @param len
 * @return std::string
 */
std::string compute_memory_hash(uint64_t start, uint64_t len);

/**
 * @brief calculate byte redundancy rate between two memory regions
 *
 * @param dst_start
 * @param src_start
 * @param len
 * @return double
 */
double compute_memcpy_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len);

/**
 * @brief calculate byte redundancy rate of memset operations
 *
 * @param start
 * @param value
 * @param len
 * @return double
 */
double compute_memset_redundancy(uint64_t start, uint32_t value, uint64_t len);

enum ValueFlowNodeType {
  VALUE_FLOW_NODE_ALLOC,
  VALUE_FLOW_NODE_MEMCPY,
  VALUE_FLOW_NODE_MEMSET,
  VALUE_FLOW_NODE_KERNEL
};

enum ValueFlowEdgeType {
  VALUE_FLOW_EDGE_ORDER,
  VALUE_FLOW_EDGE_READ
};

struct ValueFlowOp {
  // Operation id;
  uint64_t op_id;
  // Calling context id
  int32_t id;
  // Only used by kernel pcs
  uint64_t pc;
  ValueFlowNodeType type;
  double redundancy;
  std::string hash;

  ValueFlowOp() = default;

  ValueFlowOp(uint64_t op_id, int32_t id, uint64_t pc, ValueFlowNodeType type, double redundancy,
              const std::string &hash)
      : op_id(op_id), id(id), pc(pc), type(type), redundancy(redundancy), hash(hash) {}
};

struct ValueFlowRecord {
  // Pattern
  // Distribution
  // Calling context id
  int32_t id;
  // Duplicate
  std::map<int32_t, bool> duplicate;
  // Trace graph
  std::map<int32_t, std::set<int32_t>> backtrace;

  ValueFlowRecord() = default;

  ValueFlowRecord(int32_t id) : id(id) {}

  ValueFlowRecord(int32_t id, std::map<int32_t, std::set<int32_t>> &backtrace)
      : id(id), backtrace(backtrace) {}
};

struct ValueFlowNode {
  ValueFlowNodeType type;

  // Calling context id
  int32_t id;

  ValueFlowNode(ValueFlowNodeType type, int32_t id) : type(type), id(id) {} 

  ValueFlowNode() : ValueFlowNode(VALUE_FLOW_NODE_ALLOC, 0) {} 
};


class ValueFlowGraph {
 public:
  typedef std::map<int32_t, std::set<std::pair<ValueFlowEdgeType, int32_t>>> NeighborNodeMap;
  // Last update memory node
  typedef std::map<int64_t, int32_t> OpNodeMap;
  typedef std::map<int32_t, ValueFlowNode> NodeMap;

 public:
  ValueFlowGraph() {}

  typename NodeMap::const_iterator nodes_begin() const { return _nodes.begin(); }

  typename NodeMap::iterator nodes_begin() { return _nodes.begin(); }

  typename NodeMap::const_iterator nodes_end() const { return _nodes.end(); }

  typename NodeMap::iterator nodes_end() { return _nodes.end(); }

  typename OpNodeMap::const_iterator op_nodes_begin() const { return _op_nodes.begin(); }

  typename OpNodeMap::iterator op_nodes_begin() { return _op_nodes.begin(); }
  
  typename OpNodeMap::const_iterator op_nodes_end() const { return _op_nodes.end(); }

  typename OpNodeMap::iterator op_nodes_end() { return _op_nodes.end(); }

  size_t outgoing_nodes_size(int32_t node_id) const {
    if (_outgoing_nodes.find(node_id) == _outgoing_nodes.end()) {
      return 0;
    }
    return _outgoing_nodes.at(node_id).size();
  }

  const std::set<std::pair<ValueFlowEdgeType, int32_t>> &outgoing_nodes(int32_t node_id) const { return _outgoing_nodes.at(node_id); }

  size_t incoming_nodes_size(int32_t node_id) const {
    if (_incoming_nodes.find(node_id) == _incoming_nodes.end()) {
      return 0;
    }
    return _incoming_nodes.at(node_id).size();
  }

  const std::set<std::pair<ValueFlowEdgeType, int32_t>> &incoming_nodes(int32_t node_id) const { return _incoming_nodes.at(node_id); }

  bool has_edge(int32_t from, int32_t to, ValueFlowEdgeType edge_type) {
    const auto &iter = _outgoing_nodes.find(from);
    if (iter != _outgoing_nodes.end()) {
      if (iter->second.find(std::make_pair(edge_type, to)) != iter->second.end()) {
        return true;
      }
    }
    return false;
  }

  void add_edge(int32_t from, int32_t to, ValueFlowEdgeType edge_type) {
    _incoming_nodes[to].insert(std::make_pair(edge_type, from));
    _outgoing_nodes[from].insert(std::make_pair(edge_type, to));
  }

  void remove_edge(int32_t from, int32_t to, ValueFlowEdgeType edge_type) {
    _incoming_nodes[to].erase(std::make_pair(edge_type, from));
    _outgoing_nodes[from].erase(std::make_pair(edge_type, to));
  }

  void add_op_edge(uint64_t from_op, uint64_t to_op, ValueFlowEdgeType edge_type) {
    auto from_node_id = _op_nodes.at(from_op);
    auto to_node_id = _op_nodes.at(to_op);
    add_edge(from_op, to_op, edge_type);  
  }

  void add_node(int32_t node_id, const ValueFlowNode &n) { _nodes[node_id] = n; }

  bool has_node(int32_t node_id) { return _nodes.find(node_id) != _nodes.end(); }

  bool has_op_node(uint64_t op_id) { return _op_nodes.find(op_id) != _op_nodes.end(); }

  ValueFlowNode &node(int32_t node_id) { return _nodes.at(node_id); }

  void update_op_node(uint64_t op_id, int32_t node_id) {
    if (_op_nodes.find(op_id) != _op_nodes.end()) {
      auto prev_node_id = _op_nodes.at(op_id);
      add_edge(prev_node_id, node_id, VALUE_FLOW_EDGE_ORDER);
    }
    _op_nodes[op_id] = node_id;
  }

  int32_t &op_node_id(uint64_t op_id) { return _op_nodes.at(op_id); }

  size_t size() { return _nodes.size(); }

  size_t op_size() { return _op_nodes.size(); }
 
 private:
  NeighborNodeMap _incoming_nodes;
  NeighborNodeMap _outgoing_nodes;
  NodeMap _nodes;
  OpNodeMap _op_nodes;
};

bool analyze_value_flow(const ValueFlowGraph &value_flow_graph,
                        const std::vector<ValueFlowOp> &value_flow_ops,
                        std::map<int32_t, ValueFlowRecord> &value_flow_records);

void report_value_flow(const std::map<int32_t, ValueFlowRecord> &value_flow_records);

}  // namespace redshow

#endif  // REDSHOW_VALUE_FLOW_H
