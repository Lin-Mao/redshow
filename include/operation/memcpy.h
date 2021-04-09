#ifndef REDSHOW_OPERATION_MEMCPY_H
#define REDSHOW_OPERATION_MEMCPY_H

#include <string>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Memcpy : public Operation {
  u64 src_memory_op_id;
  u64 src_start;  // shadow dst start
  u64 dst_memory_op_id;
  u64 dst_start;  // shadow src start
  u64 len;

  Memcpy() : Operation(0, 0, OPERATION_TYPE_MEMCPY) {}

  Memcpy(u64 op_id, i32 ctx_id, u64 src_memory_op_id, u64 src_start, u64 dst_memory_op_id,
         u64 dst_start, u64 len)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMCPY),
        src_memory_op_id(src_memory_op_id),
        src_start(src_start),
        dst_memory_op_id(dst_memory_op_id),
        dst_start(dst_start),
        len(len) {}

  virtual ~Memcpy() {}
};

/**
 * @brief compute a redundancy for a memcpy operation
 *
 * @param update if host is updated while computing redundancy
 * @param dst_start
 * @param src_start
 * @param len
 * @return double
 */
template<bool update>
u64 compute_memcpy_redundancy(u64 dst_start, u64 src_start, u64 len);

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMCPY_H
