#ifndef REDSHOW_BINUTILS_CUBIN_H
#define REDSHOW_BINUTILS_CUBIN_H

#include "symbol.h"
#include "instruction.h"
#include "common/map.h"
#include "common/vector.h"
#include "common/utils.h"

#include <string>

namespace redshow {

struct Cubin {
  u32 cubin_id;
  std::string path;
  // <mod_id, [symbols]>
  Map<u32, SymbolVector> symbols;
  InstructionGraph inst_graph;

  Cubin() = default;

  Cubin(u32 cubin_id, const std::string &path, InstructionGraph &inst_graph)
      : cubin_id(cubin_id), path(path), inst_graph(inst_graph) {}
};

struct CubinCache {
  u32 cubin_id;
  u32 nsymbols;
  Map<u32, u64 *> symbol_pcs;
  std::string path;

  CubinCache() = default;

  CubinCache(u32 cubin_id) : cubin_id(cubin_id), nsymbols(0) {}

  CubinCache(u32 cubin_id, const std::string &path)
      : cubin_id(cubin_id), path(path), nsymbols(0) {}

  ~CubinCache() {
    for (auto &iter : symbol_pcs) {
      if (iter.second) {
        delete[] iter.second;
      }
    }
  }
};

}  // namespace redshow

#endif  // REDSHOW_BINUTILS_CUBIN_H
