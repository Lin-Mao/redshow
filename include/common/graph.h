#ifndef REDSHOW_COMMON_GRAPH_H
#define REDSHOW_COMMON_GRAPH_H

#include <map>
#include <set>

namespace redshow {

template <typename Index, typename Node, typename EdgeIndex, typename Edge>
class Graph {
 public:
  typedef std::map<Index, std::set<EdgeIndex>> NeighborNodeMap;
  typedef std::map<EdgeIndex, Edge> EdgeMap;
  typedef std::map<Index, Node> NodeMap;

 public:
  Graph() {}

  typename NodeMap::iterator nodes_begin() { return _nodes.begin(); }

  typename NodeMap::iterator nodes_end() { return _nodes.end(); }

  typename EdgeMap::iterator edges_begin() { return _edges.begin(); }

  typename EdgeMap::iterator edges_end() { return _edges.end(); }

  size_t outgoing_nodes_size(Index &index) {
    if (_outgoing_nodes.find(index) == _outgoing_nodes.end()) {
      return 0;
    }
    return _outgoing_nodes.at(index).size();
  }

  const std::set<Index> &outgoing_nodes(Index &index) { return _outgoing_nodes.at(index); }

  size_t incoming_nodes_size(Index &index) {
    if (_incoming_nodes.find(index) == _incoming_nodes.end()) {
      return 0;
    }
    return _incoming_nodes.at(index).size();
  }

  const std::set<Index> &incoming_nodes(Index &index) { return _incoming_nodes.at(index); }

  // Edge methods
  bool has_edge(EdgeIndex &edge_index) const {
    return _edges.find(edge_index) != _edges.end();
  }

  template<typename... Args>
  void add_edge(EdgeIndex &&edge_index, Args &&... edge) {
    EdgeMap::iterator iter;
    bool exist;
    std::tie(iter, inserted) =
        _edges.emplace(std::forward<EdgeIndex>(edge_index), std::forward<Args>(edge)...);
    if (inserted) {
      _incoming_nodes[to].emplace(std::forward<EdgeIndex>(edge_index));
      _outgoing_nodes[from].emplace(std::forward<EdgeIndex>(edge_index));
    }
  }

  void remove_edge(EdgeIndex &edge_index) {
    _incoming_nodes[to].erase(edge_index);
    _outgoing_nodes[from].erase(edge_index);
    _edges.erase(edge_index);
  }

  // Node methods
  bool has_node(Index &index) const { return _nodes.find(index) != _nodes.end(); }

  template<typename... Args>
  void add_node(Index &&index, Args &&... args) {
    _nodes.emplace(std::forward<Index>(index), std::forward<Args>(args)...);
  }

  void remove_node(Index &index) { _nodes.erase(index); }

  Edge &edge(EdgeIndex &index) { return _edges.at(edge); }

  Node &node(Index &index) { return _nodes.at(index); }

  size_t size() const { return _nodes.size(); }

  size_t edge_size() const { return _edges.size(); }

 private:
  NeighborNodeMap _incoming_nodes;
  NeighborNodeMap _outgoing_nodes;
  EdgeMap _edges;
  NodeMap _nodes;
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_GRAPH_H