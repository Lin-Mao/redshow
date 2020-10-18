#ifndef REDSHOW_OPERATION_KERNEL_H
#define REDSHOW_OPERATION_KERNEL_H

#include "common/utils.h"
#include "operation.h"

namespace redshow {

struct Kernel : public Operation {
  u32 cubin_id;
  u32 mod_id;
  u32 func_index;
  u64 func_addr;

  Kernel() : operation(0, 0, OPERATION_TYPE_KERNEL) {}

  Kernel(u64 op_id, u32 ctx_id u32 cubin_id, u32 mod_id, u32 func_index, u64 func_addr)
      : operation(op_id, ctx_id, OPERATION_TYPE_KERNEL),
        cubin_id(cubin_id),
        mod_id(mod_id),
        func_index(func_index),
        func_addr(func_addr) {}

  Kernel(u64 op_id, u32 ctx_id u32 cubin_id, u32 mod_id)
      : Kernel(op_id, ctx_id, cubin_id, mod_id, 0, 0) {}
};

}  // namespace redshow

#endif  // REDSHOW_OPERATION_KERNEL_H