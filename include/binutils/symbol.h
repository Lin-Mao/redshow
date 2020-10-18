#ifndef REDSHOW_BINUTILS_SYMBOL_H
#define REDSHOW_BINUTILS_SYMBOL_H

#include <optional>
#include <tuple>

#include "utils.h"
#include "common/vector.h"

namespace redshow {

struct Symbol {
  u32 index;
  u64 offset;
  u64 pc;

  /**
   * @brief Construct a new Symbol object
   *
   * @param index index in symbol table
   * @param offset offset in a binary
   * @param pc pc in memory at runtime
   */
  Symbol(u32 index, u64 offset, u64 pc) : index(index), offset(offset), pc(pc) {}

  Symbol(u32 index, u64 offset) : Symbol(index, offset, 0) {}

  Symbol(u64 pc) : Symbol(0, 0, pc) {}

  Symbol() : Symbol(0, 0, 0) {}

  bool operator<(const Symbol &other) const { return this->pc < other.pc; }
};

class SymbolVector : public Vector<Symbol> {
 public:
  bool has(const Symbol &symbol) {
    return std::find(this->begin(), this->end(), symbol) != this->end();
  }

  std::optional<std::tuple<u32, u64, u64>> transform_pc(uint64_t pc) {
    u32 function_index;
    u64 cubin_offset;
    u64 pc_offset;
    Symbol symbol(pc);

    auto symbols_iter = std::upper_bound(this->begin(), this->end(), symbol);

    if (symbols_iter != this->begin()) {
      --symbols_iter;
      pc_offset = pc - symbols_iter->pc;
      cubin_offset = pc_offset + symbols_iter->cubin_offset;
      function_index = symbols_iter->index;
    } else {
      return {};
    }

    return std::make_tuple(function_index, cubin_offset, pc_offset);
  }
};

}  // namespace redshow

#endif  // REDSHOW_BINUTILS_SYMBOL_H