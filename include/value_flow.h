#ifndef REDSHOW_VALUE_FLOW_H
#define REDSHOW_VALUE_FLOW_H

#include <string>

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

struct ValueFlowNode {
  enum Type {
    VALUE_FLOW_MEMORY,
    VALUE_FLOW_MEMSET,
    VALUE_FLOW_KERNEL
  };

  Type type;

  // Calling context id
  int32_t id;

  ValueFlowNode(Type type, int32_t id) : type(type), id(id) {} 

  ValueFlowNode() : ValueFlowNode(VALUE_FLOW_MEMORY, 0) {} 
};


class ValueFlowGraph {
 public:
  typedef std::map<int32_t, std::set<int32_t> > NeighborNodeMap;
  typedef std::map<int32_t, ValueFlowNode> NodeMap;
  // Last seen memory node
  typedef std::map<int64_t, ValueFlowNode> MemoryNodeMap;

 public:
  ValueFlowGraph() {}

  typename NodeMap::iterator nodes_begin() { return _nodes.begin(); }

  typename NodeMap::iterator nodes_end() { return _nodes.end(); }

  size_t outgoing_nodes_size(int32_t id) {
    if (_outgoing_nodes.find(id) == _outgoing_nodes.end()) {
      return 0;
    }
    return _outgoing_nodes.at(id).size();
  }

  const std::set<int32_t> &outgoing_nodes(int32_t id) { return _outgoing_nodes.at(id); }

  size_t incoming_nodes_size(int32_t id) {
    if (_incoming_nodes.find(id) == _incoming_nodes.end()) {
      return 0;
    }
    return _incoming_nodes.at(id).size();
  }

  const std::set<int32_t> &incoming_nodes(int32_t id) { return _incoming_nodes.at(id); }

  void add_edge(int32_t from, int32_t to) {
    _incoming_nodes[to].insert(from);
    _outgoing_nodes[from].insert(to);
  }

  void add_node(int32_t id, const ValueFlowNode &n) { _nodes[id] = n; }

  bool has_node(int32_t id) { return _nodes.find(id) != _nodes.end(); }

  bool has_memory_node(uint64_t memory_id) { return _memory_nodes.find(memory_id) != _memory_nodes.end(); }

  ValueFlowNode &node(int32_t id) { return _nodes.at(id); }

  void update_memory_node(uint64_t memory_id, int32_t id) { _memory_nodes[memory_id] = _nodes[id]; }

  ValueFlowNode &memory_node(uint64_t memory_id) { return _memory_nodes.at(memory_id); }

  size_t size() { return _nodes.size(); }

  size_t memory_size() { return _memory_nodes.size(); }
 
 private:
  NeighborNodeMap _incoming_nodes;
  NeighborNodeMap _outgoing_nodes;
  NodeMap _nodes;
  MemoryNodeMap _memory_nodes;
};

}  // namespace redshow

#endif  // REDSHOW_VALUE_FLOW_H
