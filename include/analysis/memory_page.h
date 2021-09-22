#ifndef REDSHOW_ANALYSIS_MEMORY_PAGE_H
#define REDSHOW_ANALYSIS_MEMORY_PAGE_H

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

namespace redshow
{

  class MemoryPage final : public Analysis
  {
  public:
    MemoryPage() : Analysis(REDSHOW_ANALYSIS_MEMORY_PAGE) {}

    virtual ~MemoryPage() = default;

    // Coarse-grained
    virtual void op_callback(OperationPtr operation);

    // Fine-grained
    virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id,
                                u32 mod_id, GPUPatchType type);

    virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

    virtual void block_enter(const ThreadId &thread_id);

    virtual void block_exit(const ThreadId &thread_id);
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

    virtual void flush(const std::string &output_dir,
                       const LockableMap<u32, Cubin> &cubins,
                       redshow_record_data_callback_func record_data_callback);

  private:
    struct ValueDistMemoryComp
    {
      bool operator()(const Memory &l, const Memory &r) const { return l.op_id < r.op_id; }
    };
    // <page_id, count>
    typedef Map<u64, u64> PageCount;
    typedef std::map<Memory, PageCount, ValueDistMemoryComp> MemoryPageCount;
    // @findhao: add conflicts count here?

    const int PAGE_SIZE = 4 * 1024;
    const int PAGE_SIZE_BITS = 12;

    struct MmeoryPageTrace final : public Trace
    {
      MemoryPageCount memory_page_count;

      MmeoryPageTrace() = default;

      virtual ~MmeoryPageTrace() {}
    };
  private:
    void get_kernel_trace(Map<u32, Map<i32, std::shared_ptr<Trace>>> &kernel_trace_p);

  private:
    static inline thread_local std::shared_ptr<MmeoryPageTrace> _trace;
  };

} // namespace redshow

#endif // REDSHOW_ANALYSIS_MEMORY_PAGE_H
