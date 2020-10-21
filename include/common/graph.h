#ifndef REDSHOW_COMMON_GRAPH_H
#define REDSHOW_COMMON_GRAPH_H

#include "common/map.h"
#include "common/set.h"

namespace redshow {

template <typename Index, typename Node, typename EdgeIndex, typename Edge>
class Graph {
 public:
  typedef Map<Index, Set<EdgeIndex>> NeighborEdgeMap;
  typedef Map<EdgeIndex, Edge> EdgeMap;
  typedef Map<Index, Node> NodeMap;

 public:
  Graph() {}

  typename NodeMap::iterator nodes_begin() { return _nodes.begin(); }

  typename NodeMap::iterator nodes_end() { return _nodes.end(); }

  typename EdgeMap::iterator edges_begin() { return _edges.begin(); }

  typename EdgeMap::iterator edges_end() { return _edges.end(); }

  size_t outgoing_edge_size(const Index &index) const noexcept {
    if (_outgoing_edges.find(index) == _outgoing_edges.end()) {
      return 0;
    }
    return _outgoing_edges.at(index).size();
  }

  const Set<EdgeIndex> &outgoing_edges(const Index &index) const noexcept {
    return _outgoing_edges.at(index);
  }

  size_t incoming_edge_size(const Index &index) const noexcept {
    if (_incoming_edges.find(index) == _incoming_edges.end()) {
      return 0;
    }
    return _incoming_edges.at(index).size();
  }

  const Set<EdgeIndex> &incoming_edges(const Index &index) const noexcept {
    return _incoming_edges.at(index);
  }

  // Edge methods
  bool has_edge(const EdgeIndex &edge_index) const noexcept { return _edges.has(edge_index); }

  template <typename... Args>
  void add_edge(EdgeIndex &&edge_index, Args &&... edge) noexcept {
    typename EdgeMap::iterator iter;
    bool inserted;
    std::tie(iter, inserted) =
        _edges.emplace(std::forward<EdgeIndex>(edge_index), std::forward<Args>(edge)...);
    if (inserted) {
      _incoming_edges[edge_index.to].emplace(std::forward<EdgeIndex>(edge_index));
      _outgoing_edges[edge_index.from].emplace(std::forward<EdgeIndex>(edge_index));
    }
  }

  void remove_edge(const EdgeIndex &edge_index) {
    _incoming_edges[edge_index.to].erase(edge_index);
    _outgoing_edges[edge_index.from].erase(edge_index);
    _edges.erase(edge_index);
  }

  // Node methods
  bool has_node(const Index &index) const noexcept { return _nodes.find(index) != _nodes.end(); }

  template <typename... Args>
  void add_node(Index &&index, Args &&... args) noexcept {
    _nodes.try_emplace(std::forward<Index>(index), std::forward<Args>(args)...);
  }

  void remove_node(const Index &index) { _nodes.erase(index); }

  Edge &edge(const EdgeIndex &edge_index) noexcept { return _edges.at(edge_index); }

  Edge &edge(const EdgeIndex &edge_index) const noexcept { return _edges.at(edge_index); }

  Node &node(const Index &index) noexcept { return _nodes.at(index); }

  Node &node(const Index &index) const noexcept { return _nodes.at(index); }

  size_t size() const noexcept { return _nodes.size(); }

  size_t edge_size() const noexcept { return _edges.size(); }

 private:
  NeighborEdgeMap _incoming_edges;
  NeighborEdgeMap _outgoing_edges;
  EdgeMap _edges;
  NodeMap _nodes;
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_GRAPH_H