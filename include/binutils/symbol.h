#ifndef REDSHOW_BINUTILS_SYMBOL_H
#define REDSHOW_BINUTILS_SYMBOL_H

#include <algorithm>
#include <optional>
#include <tuple>

#include "common/utils.h"
#include "common/vector.h"
#include "redshow.h"

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
  SymbolVector() = default;

  SymbolVector(size_t size) : Vector<Symbol>(size) {}

  std::optional<std::tuple<u32, u64, u64>> transform_pc(uint64_t pc) const {
    u32 function_index;
    u64 cubin_offset;
    u64 pc_offset;
    Symbol symbol(pc);

    auto symbols_iter = std::upper_bound(this->begin(), this->end(), symbol);

    if (symbols_iter != this->begin()) {
      --symbols_iter;
      pc_offset = pc - symbols_iter->pc;
      cubin_offset = pc_offset + symbols_iter->offset;
      function_index = symbols_iter->index;
    } else {
      return {};
    }

    return std::make_tuple(function_index, cubin_offset, pc_offset);
  }

  void transform_data_views(redshow_record_data_t &record_data) const {
    // Transform pcs
    for (auto i = 0; i < record_data.num_views; ++i) {
      uint64_t pc = record_data.views[i].pc_offset;
      auto ret = this->transform_pc(pc);
      if (ret.has_value()) {
        record_data.views[i].function_index = std::get<0>(ret.value());
        record_data.views[i].pc_offset = std::get<1>(ret.value());
      }
    }
  }
};

}  // namespace redshow

#endif  // REDSHOW_BINUTILS_SYMBOL_H