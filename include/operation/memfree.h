/**
 * @file memfree.h
 * @author @Lin-Mao
 * @brief Add for memory free operation op_callback in memory profile
 * @version 0.1
 * @date 2021-12-22
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef REDSHOW_OPERATION_MEMFREE_H
#define REDSHOW_OPERATION_MEMFREE_H

#include <memory>

#include "common/utils.h"
#include "operation/operation.h"
#include "operation/memory.h"

namespace redshow {

// struct MemoryRange {
//   u64 start;
//   u64 end;

//   MemoryRange() = default;

//   MemoryRange(u64 start, u64 end) : start(start), end(end) {}

//   bool operator<(const MemoryRange &other) const { return start < other.start; }
// };

struct Memfree : public Operation {
  MemoryRange memory_range;
  size_t len;

  Memfree() : Operation(0, 0, OPERATION_TYPE_MEMFREE) {}

  Memfree(u64 op_id, i32 ctx_id)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMFREE), len(0) {}

  Memfree(u64 op_id, i32 ctx_id, u64 start, size_t len)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMFREE), memory_range(start, start + len), len(len) {}

  // used for memory profile free with context
  Memfree(u64 op_id, i32 ctx_id, MemoryRange &memory_range)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMFREE),
        memory_range(memory_range),
        len(memory_range.end - memory_range.start) {}

  // used for memory profile
  Memfree(u64 op_id, MemoryRange &memory_range) 
      : Operation(op_id, 0, OPERATION_TYPE_MEMFREE),
        memory_range(memory_range),
        len(memory_range.end - memory_range.start) {}

  bool operator<(const Memory &other) const { return this->memory_range < other.memory_range; }

  virtual ~Memfree() {}
};

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMFREE_H
