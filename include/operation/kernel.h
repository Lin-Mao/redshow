#ifndef REDSHOW_OPERATION_KERNEL_H
#define REDSHOW_OPERATION_KERNEL_H

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct Kernel : public Operation {
  u32 cpu_thread;
  u32 stream_id;
  u32 cubin_id;
  u32 mod_id;
  u32 func_index;
  u64 func_addr;

  Kernel() : Operation(0, 0, OPERATION_TYPE_KERNEL) {}

  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread, u32 stream_id, u32 cubin_id, u32 mod_id, u32 func_index,
         u64 func_addr)
      : Operation(op_id, ctx_id, OPERATION_TYPE_KERNEL),
        cpu_thread(cpu_thread),
        stream_id(stream_id),
        cubin_id(cubin_id),
        mod_id(mod_id),
        func_index(func_index),
        func_addr(func_addr) {}

  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread, u32 stream_id, u32 cubin_id, u32 mod_id)
      : Kernel(op_id, ctx_id, cpu_thread, stream_id, cubin_id, mod_id, 0, 0) {}

  // @Lin-Mao: add stream_id in the upper constructions may lead to some unexpected problems.
  Kernel(u64 op_id, i32 ctx_id, u32 cpu_thread, u32 stream_id)
      : Kernel(op_id, ctx_id, cpu_thread, stream_id, 0, 0, 0, 0) {}

  virtual ~Kernel() {}
};

}  // namespace redshow

#endif  // REDSHOW_OPERATION_KERNEL_H
