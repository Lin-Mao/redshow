#include "analysis/torch_monitor.h"

#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>

#include "common/utils.h"
#include "operation/memcpy.h"
#include "operation/memset.h"


namespace redshow {


void TorchMonitor::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
  // Add a calling context node
  lock();


  unlock();
}

void TorchMonitor::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 stream_id,
                            u32 cubin_id, u32 mod_id, GPUPatchType type, void* aux) {
  assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

  lock();

  unlock();
}

void TorchMonitor::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void TorchMonitor::block_enter(const ThreadId &thread_id) {
  // No operation
}

void TorchMonitor::block_exit(const ThreadId &thread_id) {
  // No operation
}

void TorchMonitor::unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id,
                               const AccessKind &access_kind, const Memory &memory, u64 pc,
                               u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
}

void TorchMonitor::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void TorchMonitor::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {


}

}  // namespace redshow
