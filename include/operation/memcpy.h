#ifndef REDSHOW_OPERATION_MEMCPY_H
#define REDSHOW_OPERATION_MEMCPY_H

#include <string>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Memcpy : public Operation {
  u64 src_memory_op_id;
  u64 src_start;
  u64 src_len;
  u64 dst_memory_op_id;
  u64 dst_start;
  u64 dst_len;
  u64 len;

  Memcpy() : Operation(0, 0, OPERATION_TYPE_MEMCPY) {}

  Memcpy(u64 op_id, i32 ctx_id, u64 src_memory_op_id, u64 src_start, u64 src_len,
         u64 dst_memory_op_id, u64 dst_start, u64 dst_len, u64 len)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMCPY),
        src_memory_op_id(src_memory_op_id),
        src_start(src_start),
        src_len(src_len),
        dst_memory_op_id(dst_memory_op_id),
        dst_start(dst_start),
        dst_len(dst_len),
        len(len) {}

  virtual ~Memcpy() {}
};

/**
 * @brief compute a redundancy for a memcpy operation
 *
 * @param dst_start
 * @param src_start
 * @param len
 * @return double
 */
double compute_memcpy_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len);

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMCPY_H
