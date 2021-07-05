#include <algorithm>
#include <iostream>

#include "binutils/instruction.h"
#include "binutils/symbol.h"

void output_instructions(redshow::SymbolVector &symbol_vector,
                         redshow::InstructionGraph &inst_graph) {
  std::sort(symbol_vector.begin(), symbol_vector.end(),
            [](const redshow::Symbol &l, const redshow::Symbol &r) { return l.offset < r.offset; });

  for (auto iter = inst_graph.nodes_begin(); iter != inst_graph.nodes_end(); ++iter) {
    auto &inst = iter->second;
    if (inst.access_kind.get() != NULL) {
      auto inst_sym = redshow::Symbol(0, inst.pc);
      auto iter = std::upper_bound(symbol_vector.begin(), symbol_vector.end(), inst_sym,
                                   [](const redshow::Symbol &l, const redshow::Symbol &r) -> bool {
                                     return l.offset < r.offset;
                                   });

      if (iter != symbol_vector.begin()) {
        --iter;
        std::cout << "FUNC: " << iter->index << ", PC: 0x" << std::hex << inst.pc - iter->offset
                  << std::dec << ", ACCESS_KIND: " << inst.access_kind->to_string() << std::endl;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "./redshow_parser /path/to/instruction/file" << std::endl;
    exit(-1);
  }

  std::string file_path = std::string(argv[1]);

  redshow::InstructionGraph inst_graph;
  redshow::SymbolVector symbol_vector;
  redshow::InstructionParser::parse(file_path, symbol_vector, inst_graph);

  output_instructions(symbol_vector, inst_graph);

  return 0;
}
