#ifndef REDSHOW_ANALYSIS_TORCH_MONITOR_H
#define REDSHOW_ANALYSIS_TORCH_MONITOR_H

#include <mutex>
#include <string>

#include "analysis.h"
#include "binutils/cubin.h"
#include "common/graph.h"
#include "common/map.h"
#include "common/set.h"
#include "common/path.h"
#include "common/utils.h"
#include "operation/kernel.h"
#include "operation/memcpy.h"
#include "operation/memory.h"
#include "operation/memset.h"
#include "operation/operation.h"
#include "redshow.h"

namespace redshow {

class TorchMonitor final : public Analysis {
 public:
  TorchMonitor() : Analysis(REDSHOW_ANALYSIS_TORCH_MONITOR) {}

  virtual ~TorchMonitor() = default;

  // Coarse-grained
  virtual void op_callback(OperationPtr operation, bool is_submemory = false);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 stream_id, 
                              u32 cubin_id, u32 mode_id, GPUPatchType type, void* aux = NULL);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id,
                           const AccessKind &access_kind, const Memory &memory, u64 pc,
                           u64 value, u64 addr, u32 index, GPUPatchFlags flags);

  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

 private:
  int a;

};  // TorchMonitor

} // namespace redshow

#endif  // REDSHOW_ANALYSIS_TORCH_MONITOR_H