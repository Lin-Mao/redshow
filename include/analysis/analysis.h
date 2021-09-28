#ifndef REDSHOW_ANALYSIS_ANALYSIS_H
#define REDSHOW_ANALYSIS_ANALYSIS_H

#include <queue>
#include <string>

#include "binutils/cubin.h"
#include "trace.h"
#include "common/map.h"
// #include "operation/kernel.h"
#include "operation/memory.h"
#include "operation/operation.h"
#include "redshow.h"

namespace redshow {



class Analysis {
 public:
  Analysis(redshow_analysis_type_t type) : _type(type), _dtoh(NULL) {}

  virtual ~Analysis() = default;

  virtual void lock() { this->_lock.lock(); }

  virtual void unlock() { this->_lock.unlock(); }

  virtual void dtoh_register(redshow_tool_dtoh_func dtoh) { this->_dtoh = dtoh; }

  virtual void dtoh(u64 host, u64 device, u64 len) {
    if (this->_dtoh) {
      this->_dtoh(host, device, len);
    }
  }

  virtual void config(redshow_analysis_config_type_t config, bool enable) {
    this->_configs[config] = enable;
  }

  // Coarse-grained
  virtual void op_callback(OperationPtr operation) = 0;

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type) = 0;

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id) = 0;

  virtual void block_enter(const ThreadId &thread_id) = 0;

  virtual void block_exit(const ThreadId &thread_id) = 0;

  /**
   * @brief A callback for every unit
   *
   * @param kernel_id kernel context id
   * @param thread_id GPU thread id
   * @param access_kind unit size, vec size, and access type
   * @param memory memory object
   * @param pc instruction pc
   * @param value unit value
   * @param addr start access address
   * @param index unit index
   * @param read read/write
   */
  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) = 0;

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) = 0;

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) = 0;

 protected:
  Map<u32, Map<i32, std::shared_ptr<Trace>>> _kernel_trace;
  Map<redshow_analysis_config_type_t, bool> _configs;
  redshow_tool_dtoh_func _dtoh;
  redshow_analysis_type_t _type;
  std::mutex _lock;
};

struct CompareView {
  bool operator()(redshow_record_view_t const &r1, redshow_record_view_t const &r2) {
    return r1.red_count > r2.red_count;
  }
};

typedef std::priority_queue<redshow_record_view_t, Vector<redshow_record_view_t>, CompareView>
    TopViews;

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_ANALYSIS_H
