/**
 * @file memory_profile.h
 * @author @Lin-Mao
 * @brief Header file for memory_profile.cpp
 * @version 0.1
 * @date 2021-09-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef REDSHOW_ANALYSIS_MEMORY_PROFILE_H
#define REDSHOW_ANALYSIS_MEMORY_PROFILE_H

#include "analysis.h"

namespace redshow {

class MemoryProfile final : public Analysis {
  public:
  MemoryProfile() : Analysis(REDSHOW_ANALYSIS_VALUE_PATTERN) {}

  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

  ~MemoryProfile() {}


/***********************************************************************
 *                 private elements area
 * ********************************************************************/

  private:

  struct MemoryProfileTrace final : public Trace {
    // only need to know memory access, don't care read or write
    // here use memory range to loge access range but not allocation and sub-allocation

    // u64: Memory:Operation->op_id
    Map<u64, Set<MemoryRange>> read_memory;
    Map<u64, Set<MemoryRange>> write_memory;

    MemoryProfileTrace() = default;

    virtual ~MemoryProfileTrace() {}
  };

  std::shared_ptr<MemoryProfileTrace> _trace;




}; // class MemoryProfile

} // namespace redshow

#endif // REDSHOW_ANALYSIS_MEMORY_PROFILE_H 