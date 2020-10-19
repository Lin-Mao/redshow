#ifndef REDSHOW_OPERATION_MEMCPY_H
#define REDSHOW_OPERATION_MEMCPY_H

#include <string>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Memcpy : public Operation {
  u64 src_memory_op_id;
  u64 dst_memory_op_id;
  std::string hash;
  double redundancy;

  Memcpy() : Operation(0, 0, OPERATION_TYPE_MEMCPY) {}

  Memcpy(u64 op_id, u32 ctx_id, uint32_t src_memory_op_id, uint32_t dst_memory_op_id,
         const std::string &hash, double redundancy)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMCPY),
        src_memory_op_id(src_memory_op_id),
        dst_memory_op_id(dst_memory_op_id),
        hash(hash),
        redundancy(redundancy) {}
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
}

}  // namespace redshow

#endif  // REDSHOW_OPERATION_MEMCPY_H