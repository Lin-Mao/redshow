/**
 * @file memory_profile.cpp
 * @author @Lin-Mao
 * @brief New mode in GVPorf for memory profiling
 * @version 0.1
 * @date 2021-09-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include "analysis/memory_profile.h"

namespace redshow {

void MemoryProfile::op_callback(OperationPtr op) {
// TODO(@Mao):
}

void MemoryProfile::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {
    // Do not need to know value and need to get interval of memory
    assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

    lock();

    if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<MemoryProfileTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
    }

    _trace = std::dynamic_pointer_cast<MemoryProfileTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

    unlock();
}

void MemoryProfile::analysis_end(u32 cpu_thread, i32 kernel_id) {

}

void MemoryProfile::block_enter(const ThreadId &thread_id) {

}

void MemoryProfile::block_exit(const ThreadId &thread_id) {

}

void MemoryProfile::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {
// TODO(@Mao): 
    if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  auto &memory_range = memory.memory_range;
  if (flags & GPU_PATCH_READ) {
    if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
    //   merge_memory_range(_trace->read_memory[memory.op_id], memory_range);
    } else if (_trace->read_memory[memory.op_id].empty()) {
      _trace->read_memory[memory.op_id].insert(memory_range);
    }
  }
  if (flags & GPU_PATCH_WRITE) {
    // merge_memory_range(_trace->write_memory[memory.op_id], memory_range);
  }


}

  // Flush
void MemoryProfile::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {

}

void MemoryProfile::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {

}

}   // namespace redshow 