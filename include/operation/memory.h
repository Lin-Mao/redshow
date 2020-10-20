#ifndef REDSHOW_OPERATION_MEMORY_H
#define REDSHOW_OPERATION_MEMORY_H

#include <memory>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct MemoryRange {
  u64 start;
  u64 end;

  MemoryRange() = default;

  MemoryRange(u64 start, u64 end) : start(start), end(end) {}

  bool operator<(const MemoryRange &other) const { return start < other.start; }
};

struct Memory : public Operation {
  MemoryRange memory_range;
  size_t len;
  std::shared_ptr<uint8_t[]> value;

  Memory() : Operation(0, 0, OPERATION_TYPE_MEMORY) {}

  Memory(u64 op_id, u32 ctx_id, MemoryRange &memory_range)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMORY),
        memory_range(memory_range),
        len(memory_range.end - memory_range.start),
        value(new uint8_t[len]) {}
  
  virtual ~Memory() {}
};

/**
 * @brief calculate a hash for the memory region
 *
 * @param start
 * @param len
 * @return std::string
 */
std::string compute_memory_hash(uint64_t start, uint64_t len);

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMORY_H
