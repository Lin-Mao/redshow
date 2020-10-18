#ifndef REDSHOW_ANALYSIS_ANALYSIS_H
#define REDSHOW_ANALYSIS_ANALYSIS_H

#include "redshow.h"

namespace redshow {

class Analysis {
 public:
  Analysis(redshow_access_type_t type) : _type(type) {}
  
  // Coarse-grained
  virtual void op_callback(Operation &operation) = 0;

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id) = 0;

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id) = 0;

  virtual void block_enter(const ThreadId &thread_id) = 0;

  virtual void block_exit(const ThreadId &thread_id) = 0;

  virtual void unit_access(i32 kernel_id, const Memory &memory, const ThreadId &thread_id,
                           const AccessKind &access_kind, u64 value, u32 stride, u32 index,
                           bool read) = 0;

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const Map<u32, Cubin> &cubins,
                            redshow_record_data_callback_func *record_data_callback) = 0;

  virtual void flush(const Map<u32, Cubin> &cubins, const std::string &output_dir,
                     const std::vector<OperationPtr> operations,
                     redshow_record_data_callback_func *record_data_callback) = 0;

  virtual ~Analysis() {}
 
 protected:
  redshow_analysis_type_t _type;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_ANALYSIS_H