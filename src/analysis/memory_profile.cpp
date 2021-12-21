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

void MemoryProfile::merge_memory_range(Set<MemoryRange> &memory, const MemoryRange &memory_range) {
  auto start = memory_range.start;
  auto end = memory_range.end;

  auto liter = memory.prev(memory_range);
  if (liter != memory.end()) { // @Mao: xxx.end is not the last iterator but the next of the last iterator and has no meaning
    if (liter->end >= memory_range.start) {
      if (liter->end < memory_range.end) {
        // overlap and not covered
        start = liter->start;
        liter = memory.erase(liter);
      } else {
        // Fully covered
        return;
      }
    } else {
      liter++;
    }
  }

  bool range_delete = false;
  auto riter = liter;
  if (riter != memory.end()) {
    if (riter->start <= memory_range.end) {
      if (riter->end < memory_range.end) {
        // overlap and not covered
        range_delete = true;
        riter = memory.erase(riter);
      } else if (riter->start == memory_range.start) {
        // riter->end >= memory_range.end
        // Fully covered
        return;
      } else {
        // riter->end >= memory_range.end
        // Partial covered
        end = riter->end;
        riter = memory.erase(riter);
      }
    }
  }

  while (range_delete) {
    range_delete = false;
    if (riter != memory.end()) {
      if (riter->start <= memory_range.end) {
        if (riter->end < memory_range.end) {
          // overlap and not covered
          range_delete = true;
        } else {
          // Partial covered
          end = riter->end;
        }
        riter = memory.erase(riter);
      }
    }
  }

  if (riter != memory.end()) {
    // Hint for constant time insert: emplace(h, p) if p is before h
    memory.emplace_hint(riter, start, end);
  } else {
    memory.emplace(start, end);
  }
}

// not a whole buffer, but a part buffer in a memory object
void MemoryProfile::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {
if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  auto &memory_range = memory.memory_range;
  if (flags & GPU_PATCH_READ) {
    if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
      merge_memory_range(_trace->read_memory[memory.op_id], memory_range);
    } else if (_trace->read_memory[memory.op_id].empty()) {
      _trace->read_memory[memory.op_id].insert(memory_range);
    }
  }
  if (flags & GPU_PATCH_WRITE) {
    merge_memory_range(_trace->write_memory[memory.op_id], memory_range);
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