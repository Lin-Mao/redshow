#ifndef REDSHOW_BINUTILS_INSTRUCTION_H
#define REDSHOW_BINUTILS_INSTRUCTION_H

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "common/graph.h"
#include "common/utils.h"
#include "symbol.h"
#include "redshow.h"

namespace redshow {

struct AccessKind {
  // 8, 16, 32, 64, 128
  uint32_t vec_size;
  // 8, 16, 32, 64
  uint32_t unit_size;
  redshow_data_type_t data_type;

  AccessKind(uint32_t unit_size, uint32_t vec_size, redshow_data_type_t data_type)
      : unit_size(unit_size), vec_size(vec_size), data_type(data_type) {}

  AccessKind() : AccessKind(0, 0, REDSHOW_DATA_UNKNOWN) {}

  u64 value_to_basic_type(u64 a, int decimal_degree_f32, int decimal_degree_f64);

  std::string value_to_string(u64 a, bool is_signed);

  std::string to_string() {
    std::stringstream ss;
    if (data_type == REDSHOW_DATA_UNKNOWN) {
      ss << "UNKNOWN";
    } else if (data_type == REDSHOW_DATA_INT) {
      ss << "INTEGER";
    } else if (data_type == REDSHOW_DATA_FLOAT) {
      ss << "FLOAT";
    }
    ss << ",v:" << vec_size;
    ss << ",u:" << unit_size;
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
  int predicate;          // P0-P6
  std::vector<int> dsts;  // R0-R255: only records normal registers
  std::vector<int> srcs;  // R0-R255, only records normal registers
  std::map<int, std::vector<int> > assign_pcs;
  std::shared_ptr<AccessKind> access_kind;

  Instruction(const std::string &op, unsigned int pc, int predicate, std::vector<int> &dsts,
              std::vector<int> &srcs, std::map<int, std::vector<int> > &assign_pcs)
      : op(op),
        pc(pc),
        predicate(predicate),
        dsts(dsts),
        srcs(srcs),
        assign_pcs(assign_pcs),
        access_kind(NULL) {}

  Instruction() : access_kind(NULL) {}

  bool operator<(const Instruction &other) const { return this->pc < other.pc; }
};

struct InstructionDependency {
  bool inter_function = false;

  InstructionDependency() = default;

  InstructionDependency(bool inter_function) : inter_function(inter_function) {}
};

struct InstructionDependencyIndex {
  u64 from;
  u64 to;

  InstructionDependencyIndex() = default;

  InstructionDependencyIndex(u64 from, u64 to) : from(from), to(to) {}

  bool operator<(const InstructionDependencyIndex &other) const {
    if (this->from == other.from) {
      return this->to < other.to;
    }
    return this->from < other.from;
  }
};

typedef Graph<u64, Instruction, InstructionDependencyIndex, InstructionDependency> InstructionGraph;

class InstructionParser {
 public:
  InstructionParser() = default;
  /**
   * @brief instruction parsing interface
   *
   * @param file_path
   * @param symbols
   * @param graph
   * @return true
   * @return false
   */
  static bool parse(const std::string &file_path, SymbolVector &symbols, InstructionGraph &graph);

 private:
  static void default_access_kind(Instruction &inst);

  static AccessKind init_access_kind(Instruction &inst, InstructionGraph &inst_graph,
                                     std::set<unsigned int> &visited, bool load);
};

}  // namespace redshow

#endif  // REDSHOW_INSTRUCTION_H
