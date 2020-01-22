#ifndef _INSTRUCTION_STAT_H_
#define _INSTRUCTION_STAT_H_

#include <map>
#include <string>
#include <vector>

/*
 * A copy-paste struct from hpctoolkit
 */

struct InstructionStat {
  std::string op; 
  unsigned int pc; 
  int predicate;  // P0-P6
  std::vector<int> dsts;  // R0-R255: only records normal registers
  std::vector<int> srcs;  // R0-R255, only records normal registers
  std::map<int, std::vector<int> > assign_pcs;
  std::map<int, std::vector<std::vector<int> > > assign_pc_paths;

  InstructionStat() {}

  InstructionStat(const std::string &op,
    unsigned int pc, int predicate, std::vector<int> &dsts, std::vector<int> &srcs) :
    op(op), pc(pc), predicate(predicate), dsts(dsts),
    srcs(srcs) {}

  InstructionStat(const std::string &op,
    unsigned int pc, int predicate, std::vector<int> &dsts, std::vector<int> &srcs,
    std::map<int, std::vector<int> > &assign_pcs) :
    op(op), pc(pc), predicate(predicate), dsts(dsts),
    srcs(srcs), assign_pcs(assign_pcs) {}

  bool operator < (const InstructionStat &other) const {
    return this->pc < other.pc;
  }
};


/*
 * A function modified from hpctoolkit
 */
bool read_instruction_stats(const std::string &file_path, std::vector<InstructionStat> &inst_stats);

#endif  // _INSTRUCTION_STAT_H_
