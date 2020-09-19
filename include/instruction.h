#ifndef REDSHOW_INSTRUCTION_H
#define REDSHOW_INSTRUCTION_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <sstream>
#include <vector>

#include "redshow.h"

struct Symbol {
  uint32_t index;
  uint64_t cubin_offset;
  uint64_t pc;

  Symbol(uint32_t index, uint64_t cubin_offset, uint64_t pc) :
      index(index), cubin_offset(cubin_offset), pc(pc) {}

  Symbol(uint32_t index, uint64_t cubin_offset) :
      Symbol(index, cubin_offset, 0) {}

  Symbol(uint64_t pc) : Symbol(0, 0, pc) {}

  Symbol() : Symbol(0, 0, 0) {}

  bool operator<(const Symbol &other) const {
    return this->pc < other.pc;
  }
};


struct AccessKind {
  // 8, 16, 32, 64, 128
  uint32_t vec_size;
  // 8, 16, 32, 64
  uint32_t unit_size;
  redshow_data_type_t data_type;

  AccessKind(uint32_t unit_size, uint32_t vec_size, redshow_data_type_t data_type) :
      unit_size(unit_size), vec_size(vec_size), data_type(data_type) {}

  AccessKind() : AccessKind(0, 0, REDSHOW_DATA_UNKNOWN) {}

  std::string to_string() {
    std::stringstream ss;
    if (data_type == REDSHOW_DATA_UNKNOWN) {
      ss << "UNKNOWN";
    } else if (data_type == REDSHOW_DATA_INT) {
      ss << "INTEGER";
    } else if (data_type == REDSHOW_DATA_FLOAT) {
      ss << "FLOAT";
    }
    ss << " v:" << vec_size;
    ss << " u:" << unit_size;
    return ss.str();
  }

  bool operator<(const AccessKind &other) const {
    if (this->vec_size == other.vec_size) {
      if (this->unit_size == other.unit_size) {
        return this->data_type < other.data_type;
      }
      return this->unit_size < other.unit_size;
    }
    return this->vec_size < other.vec_size;
  }

};

/*
 * A copy-paste struct from hpctoolkit
 */

struct Instruction {
  std::string op;
  unsigned int pc;
  int predicate;  // P0-P6
  std::vector<int> dsts;  // R0-R255: only records normal registers
  std::vector<int> srcs;  // R0-R255, only records normal registers
  std::map<int, std::vector<int> > assign_pcs;
  std::shared_ptr<AccessKind> access_kind;

  Instruction(const std::string &op, unsigned int pc, int predicate,
              std::vector<int> &dsts, std::vector<int> &srcs,
              std::map<int, std::vector<int> > &assign_pcs) :
      op(op), pc(pc), predicate(predicate),
      dsts(dsts), srcs(srcs), assign_pcs(assign_pcs),
      access_kind(NULL) {}

  Instruction() : access_kind(NULL) {}

  bool operator<(const Instruction &other) const {
    return this->pc < other.pc;
  }
};


class InstructionGraph {
public:
  typedef std::map<unsigned int, std::set<unsigned int> > NeighborNodeMap;
  typedef std::map<unsigned int, Instruction> NodeMap;

public:
  InstructionGraph() {}

  typename NodeMap::iterator nodes_begin() {
    return _nodes.begin();
  }

  typename NodeMap::iterator nodes_end() {
    return _nodes.end();
  }

  size_t outgoing_nodes_size(unsigned int pc) {
    if (_outgoing_nodes.find(pc) == _outgoing_nodes.end()) {
      return 0;
    }
    return _outgoing_nodes.at(pc).size();
  }

  const std::set<unsigned int> &outgoing_nodes(unsigned int pc) {
    return _outgoing_nodes.at(pc);
  }

  size_t incoming_nodes_size(unsigned int pc) {
    if (_incoming_nodes.find(pc) == _incoming_nodes.end()) {
      return 0;
    }
    return _incoming_nodes.at(pc).size();
  }

  const std::set<unsigned int> &incoming_nodes(unsigned int pc) {
    return _incoming_nodes.at(pc);
  }

  void add_edge(unsigned int from, unsigned int to) {
    _incoming_nodes[to].insert(from);
    _outgoing_nodes[from].insert(to);
  }


  void add_node(unsigned int pc, const Instruction &inst) {
    _nodes[pc] = inst;
  }

  bool has_node(unsigned int pc) {
    return _nodes.find(pc) != _nodes.end();
  }

  Instruction &node(unsigned int pc) {
    return _nodes.at(pc);
  }

  size_t size() {
    return _nodes.size();
  }

private:
  NeighborNodeMap _incoming_nodes;
  NeighborNodeMap _outgoing_nodes;
  NodeMap _nodes;
};


/*
 * A function modified from hpctoolkit
 */
bool parse_instructions(const std::string &file_path, std::vector<Symbol> &symbols, InstructionGraph &graph);

#endif  // REDSHOW_INSTRUCTION_H
