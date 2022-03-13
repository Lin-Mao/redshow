#ifndef REDSHOW_ANALYSIS_MEMORY_ACCESS_H
#define REDSHOW_ANALYSIS_MEMORY_ACCESS_H

#include <algorithm>
#include <fstream>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <regex>
#include <set>
#include <string>
#include <tuple>

#include "analysis.h"
#include "binutils/instruction.h"
#include "binutils/real_pc.h"
#include "common/map.h"
#include "common/utils.h"
#include "common/vector.h"
#include "redshow.h"

namespace redshow {
typedef Map<redshow::MemoryRange, std::shared_ptr<redshow::Memory>> MemoryMap;
class MemoryAccess final : public Analysis {
 public:
  MemoryAccess() : Analysis(REDSHOW_ANALYSIS_MEMORY_ACCESS) {}

  virtual ~MemoryAccess() = default;

 public:
  // private:
  struct ValueDistMemoryComp {
    bool operator()(const Memory &l, const Memory &r) const { return l.op_id < r.op_id; }
  };
  // <page_id, count>
  typedef Map<u64, u64> AccessCount;
  typedef std::map<Memory, AccessCount, ValueDistMemoryComp> MemoryAccessCount;

  LockableMap<uint64_t, MemoryMap> *memory_snapshot_p;
  const int PAGE_SIZE = 4 * 1024;
  const int PAGE_SIZE_BITS = 12;
  struct MemoryAccessTrace final : public Trace {
    MemoryAccessCount memory_access_count;

    MemoryAccessTrace() = default;

    virtual ~MemoryAccessTrace() {}
  };
  // private:
  static inline std::shared_ptr<MemoryAccessTrace> _trace;
  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id,
                              u32 mod_id, GPUPatchType type);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void function_call(const ThreadId &thread_id, u64 pc, u64 target_pc);

  virtual void function_return(const ThreadId &thread_id, u64 pc, u64 target_pc);

  //  Since we don't use value, the value here will always be 0.
  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id,
                           const AccessKind &access_kind, const Memory &memory,
                           u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  // Flush
  virtual void
  flush_thread(u32 cpu_thread, const std::string &output_dir,
               const LockableMap<u32, Cubin> &cubins,
               redshow_record_data_callback_func record_data_callback);

  // Flush
  virtual void
  flush_now(u32 cpu_thread, const std::string &output_dir,
            const LockableMap<u32, Cubin> &cubins,
            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir,
                     const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);
};
}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_MEMORY_ACCESS_H
