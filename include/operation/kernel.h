#ifndef REDSHOW_OPERATION_KERNEL_H
#define REDSHOW_OPERATION_KERNEL_H

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Kernel : public Operation {
  u32 cpu_thread;
  u32 cubin_id;
  u32 mod_id;
  u32 func_index;
  u64 func_addr;

  Kernel() : Operation(0, 0, OPERATION_TYPE_KERNEL) {}

  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread, u32 cubin_id, u32 mod_id, u32 func_index,
         u64 func_addr)
      : Operation(op_id, ctx_id, OPERATION_TYPE_KERNEL),
        cpu_thread(cpu_thread),
        cubin_id(cubin_id),
        mod_id(mod_id),
        func_index(func_index),
        func_addr(func_addr) {}

  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread, u32 cubin_id, u32 mod_id)
      : Kernel(op_id, ctx_id, cpu_thread, cubin_id, mod_id, 0, 0) {}

  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread) : Kernel(op_id, ctx_id, cpu_thread, 0, 0, 0, 0) {}

  virtual ~Kernel() {}
};

}  // namespace redshow

#endif  // REDSHOW_OPERATION_KERNEL_H
