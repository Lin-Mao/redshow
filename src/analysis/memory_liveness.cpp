/**
 * @file memory_liveness.cpp
 * @author @Lin-Mao
 * @brief Split the liveness part from memory profile mode. Faster to get the liveness.
 * @version 0.1
 * @date 2022-03-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "analysis/memory_liveness.h"

namespace redshow {

void MemoryLiveness::op_callback(OperationPtr op, bool is_submemory) {
  lock();
  
  if (op->type == OPERATION_TYPE_KERNEL) {
    kernel_op_callback(std::dynamic_pointer_cast<Kernel>(op));
  } else if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op), is_submemory);
  } else if (op->type == OPERATION_TYPE_MEMFREE) {
    memfree_op_callback(std::dynamic_pointer_cast<Memfree>(op), is_submemory);
  } else if (op->type == OPERATION_TYPE_MEMCPY) {
    memcpy_op_callback(std::dynamic_pointer_cast<Memcpy>(op));
  } else if (op->type == OPERATION_TYPE_MEMSET) {
    memset_op_callback(std::dynamic_pointer_cast<Memset>(op));
  }

  unlock();
}

void MemoryLiveness::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {

}

void MemoryLiveness::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {

}

void MemoryLiveness::kernel_op_callback(std::shared_ptr<Kernel> op) {

}

void MemoryLiveness::memcpy_op_callback(std::shared_ptr<Memcpy> op) {

}

void MemoryLiveness::memset_op_callback(std::shared_ptr<Memset> op) {

}


void MemoryLiveness::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type) {}

void MemoryLiveness::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void MemoryLiveness::block_enter(const ThreadId &thread_id) {}

void MemoryLiveness::block_exit(const ThreadId &thread_id) {}

void MemoryLiveness::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {}

void MemoryLiveness::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void MemoryLiveness::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {}


}   // namespace redshow