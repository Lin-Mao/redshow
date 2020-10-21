#ifndef REDSHOW_OPERATION_MEMSET_H
#define REDSHOW_OPERATION_MEMSET_H

#include <string>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Memset : public Operation {
  u64 memory_op_id;
  std::string hash;
  double redundancy;
  double overwrite;

  Memset() : Operation(0, 0, OPERATION_TYPE_MEMSET) {}

  Memset(u64 op_id, u32 ctx_id, u64 memory_op_id, const std::string &hash, double redundancy,
         double overwrite)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMSET),
        memory_op_id(memory_op_id),
        hash(hash),
        redundancy(redundancy),
        overwrite(overwrite) {}

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
