#ifndef REDSHOW_ANALYSIS_TORCH_MONITOR_H
#define REDSHOW_ANALYSIS_TORCH_MONITOR_H

#include <mutex>
#include <string>

#include "analysis.h"
#include "binutils/cubin.h"
#include "common/map.h"
#include "common/utils.h"
#include "operation/kernel.h"
#include "operation/memcpy.h"
#include "operation/memory.h"
#include "operation/memset.h"
#include "operation/memfree.h"
#include "operation/operation.h"
#include "redshow.h"

#include <fstream>

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
  Map<u64, std::shared_ptr<Memory>> _memories;

  Map<u64, std::shared_ptr<Memory>> _current_memories;

  // <start_addr, memory_op_id>
  Map<u64, u64> _addresses_map;

  u64 _current_memory_usage = 0;  // to update _memory_peak
  u64 _memory_peak = 0;
  u64 _optimal_memory_peak = 0;
  u64 _memory_peak_kernel = 0;

  u64 _nums_cudamalloc = 0;
  u64 _nums_cudafree = 0;




  Map<u64, std::shared_ptr<Memory>> _submemories;

  Map<u64, std::shared_ptr<Memory>> _current_submemories;

  Map<u64, u64> _sub_addresses_map;

  u64 _current_submemory_usage = 0;  // to update _submemory_peak
  u64 _submemory_peak = 0;
  u64 _optimal_submemory_peak = 0;
  u64 _submemory_peak_kernel = 0;


 private:
  void memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory = false);

  void memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory = false);

  void kernel_op_callback(std::shared_ptr<Kernel> op);

  void memcpy_op_callback(std::shared_ptr<Memcpy> op);

  void memset_op_callback(std::shared_ptr<Memset> op);

  void update_aux_hit(void* aux, u64 kernel_op_id, bool is_sub = false);

};  // TorchMonitor

} // namespace redshow

#endif  // REDSHOW_ANALYSIS_TORCH_MONITOR_H