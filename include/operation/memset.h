#ifndef REDSHOW_OPERATION_MEMSET_H
#define REDSHOW_OPERATION_MEMSET_H

#include <string>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Memset : public Operation {
  u64 memory_op_id;
  u64 shadow_start;
  u64 shadow_len;
  u64 value;
  u64 len;

  Memset() : Operation(0, 0, OPERATION_TYPE_MEMSET) {}

  Memset(u64 op_id, i32 ctx_id, u64 memory_op_id, u64 shadow_start, u64 shadow_len, u64 value,
         u64 len)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMSET),
        memory_op_id(memory_op_id),
        shadow_start(shadow_start),
        shadow_len(shadow_len),
        value(value),
        len(len) {}

  virtual ~Memset() {}
};

/**
 * @brief Compute a redundancy for a memset operation
 *
 * @param start
 * @param value
 * @param len
 * @return double
 */
double compute_memset_redundancy(uint64_t start, uint32_t value, uint64_t len);

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMSET_H
