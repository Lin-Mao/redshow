//
// Created by find on 19-7-1.
//

#include "analysis/memory_page.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <tuple>
#include <utility>

#include "common/utils.h"
#include "common/vector.h"
#include "operation/kernel.h"
#include "redshow.h"

namespace redshow {

void MemoryPage::op_callback(OperationPtr operation) {
  // Do nothing
}

// Fine-grained
void MemoryPage::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                                GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_DEFAULT);

  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<MmeoryPageTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<MmeoryPageTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void MemoryPage::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void MemoryPage::block_enter(const ThreadId &thread_id) {
  // Do nothing
}

void MemoryPage::block_exit(const ThreadId &thread_id) {
  // Do nothing
}

void MemoryPage::unit_access(i32 kernel_id, const ThreadId &thread_id,
                             const AccessKind &access_kind, const Memory &memory, u64 pc,
                             u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  addr += index * access_kind.unit_size / 8;
  // @todo: gpu memory page size is various.
  u64 page_index = (addr - memory.memory_range.start) >> PAGE_SIZE_BITS;
  auto &memory_page_count = _trace->memory_page_count;
  memory_page_count[memory][page_index] += 1;
}
void MemoryPage::get_kernel_trace(Map<u32, Map<i32, std::shared_ptr<Trace>>> &kernel_trace_p) {
  //  An empty function for drcctprof to capture.
}

void MemoryPage::flush_thread(u32 cpu_thread, const std::string &output_dir,
                              const LockableMap<u32, Cubin> &cubins,
                              redshow_record_data_callback_func record_data_callback) {
  if (!this->_kernel_trace.has(cpu_thread)) {
    return;
  }

  lock();

  auto &thread_kernel_trace = this->_kernel_trace.at(cpu_thread);

  unlock();
  get_kernel_trace(this->_kernel_trace);
}

void MemoryPage::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                       redshow_record_data_callback_func record_data_callback) {}

}  // namespace redshow
