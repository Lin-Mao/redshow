#ifndef REDSHOW_BINUTILS_SYMBOL_H
#define REDSHOW_BINUTILS_SYMBOL_H

#include <algorithm>
#include <optional>
#include <tuple>

#include "common/utils.h"
#include "common/vector.h"
#include "real_pc.h"
#include "redshow.h"

namespace redshow {

struct Symbol {
  u32 cubin_id;
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

  std::optional<RealPC> transform_pc(uint64_t pc) const {
    Symbol symbol(pc);
    RealPC real_pc;

    auto symbols_iter = std::upper_bound(this->begin(), this->end(), symbol);

    if (symbols_iter != this->begin()) {
      --symbols_iter;
      real_pc.pc_offset = pc - symbols_iter->pc;
      real_pc.cubin_offset = real_pc.pc_offset + symbols_iter->offset;
      real_pc.function_index = symbols_iter->index;
      real_pc.cubin_id = symbols_iter->cubin_id;
    } else {
      return {};
    }

    return real_pc;
  }

  void transform_data_views(redshow_record_data_t &record_data) const {
    // Transform pcs
    for (auto i = 0; i < record_data.num_views; ++i) {
      uint64_t pc = record_data.views[i].pc_offset;
      auto ret = this->transform_pc(pc);
      if (ret.has_value()) {
        record_data.views[i].function_index = ret.value().function_index;
        record_data.views[i].pc_offset = ret.value().pc_offset;
      }
    }
  }
};

}  // namespace redshow

#endif  // REDSHOW_BINUTILS_SYMBOL_H
