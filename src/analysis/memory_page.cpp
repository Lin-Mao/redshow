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

using redshow::LockableMap;
using redshow::Map;
using redshow::MemoryMap;
//  An empty function for drcctprof to capture.
void get_kernel_trace(redshow::u32 cpu_thread, Map<redshow::i32, std::shared_ptr<redshow::Trace>> &kernel_trace_p, LockableMap<uint64_t, MemoryMap> &memory_snapshot_p) {
}
//  An empty function for drcctprof to capture.
void get_memory_map(MemoryMap *memory_map) {
}

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
    auto trace = std::make_shared<MemoryPageTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<MemoryPageTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void MemoryPage::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void MemoryPage::block_enter(const ThreadId &thread_id) {
  // Do nothing
}

void MemoryPage::block_exit(const ThreadId &thread_id) {
  // Do nothing
}
void MemoryPage::set_memory_snapshot_p(LockableMap<uint64_t, MemoryMap> *a_memory_snapshot_p) {
  memory_snapshot_p = a_memory_snapshot_p;
}

void MemoryPage::unit_access(i32 kernel_id, const ThreadId &thread_id,
                             const AccessKind &access_kind, const Memory &memory, u64 pc,
                             u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  addr += index * access_kind.unit_size / 8;
  // @todo: gpu memory page size is various.
  u64 page_index = addr >> PAGE_SIZE_BITS;
  auto &memory_page_count = _trace->memory_page_count;
  memory_page_count[memory][page_index] += 1;
}

using std::cout;
using std::endl;
void MemoryPage::flush_thread(u32 cpu_thread, const std::string &output_dir,
                              const LockableMap<u32, Cubin> &cubins,
                              redshow_record_data_callback_func record_data_callback) {
  if (!this->_kernel_trace.has(cpu_thread)) {
    return;
  }

  lock();

  auto thread_kernel_trace = this->_kernel_trace.at(cpu_thread);
  unlock();
  get_kernel_trace(cpu_thread, this->_kernel_trace.at(cpu_thread), *(this->memory_snapshot_p));
  // for (auto item : this->_kernel_trace) {
  //   cout << "cpu thread id " << item.first << endl;
  //   for (auto item2 : item.second) {
  //     cout << "kernel id " << item2.first << endl;
  //     auto trace = std::dynamic_pointer_cast<MemoryPageTrace>(item2.second);
  //     auto mpc = trace->memory_page_count;
  //     cout << "size: " << mpc.size() <<endl;
  //     for (auto item3 : mpc) {
  //       // item3: <Memory, PageCount>
  //       cout<<&item3<<endl;
  //       cout<<item3.first.len<<endl;
  //       cout << item3.first.len << "\t" << item3.first.memory_range.start << endl;
  //       cout << item3.first.memory_range.start << " ";
  //     }
  //   }
  //   cout << "===========" << endl;
  // }
}

void MemoryPage::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                       redshow_record_data_callback_func record_data_callback) {}

}  // namespace redshow
