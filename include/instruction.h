#ifndef _INSTRUCTION_H_
#define _INSTRUCTION_H_

#include <map>
#include <set>
#include <string>
#include <vector>

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

  Instruction(const std::string &op, unsigned int pc, int predicate,
    std::vector<int> &dsts, std::vector<int> &srcs,
    std::map<int, std::vector<int> > &assign_pcs) :
    op(op), pc(pc), predicate(predicate),
    dsts(dsts), srcs(srcs), assign_pcs(assign_pcs) {}

  Instruction() {}

  bool operator < (const Instruction &other) const {
    return this->pc < other.pc;
  }

  bool operator == (const Instruction &other) const {
    return this->pc == other.pc;
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

  const std::set<unsigned int> &outgoing_nodes(unsigned int pc) {
    return _outgoing_nodes.at(pc);
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


struct AccessType {
  enum DataType {
    UNKNOWN = 0,
    INTEGER = 1,
    FLOAT = 2
  };

  // 16, 32, 64
  size_t vec_size;
  DataType type; 
};


/*
 * A function modified from hpctoolkit
 */
bool parse_instruction_graph(const std::string &file_path, InstructionGraph &graph);

AccessType load_data_type(unsigned int pc, InstructionGraph &graph);

AccessType store_data_type(unsigned int pc, InstructionGraph &graph);

#endif  // _INSTRUCTION_H_
