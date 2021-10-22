//
// Created by find on 19-7-1.
//

#ifndef REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY_H
#define REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY_H

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

class TemporalRedundancy final : public Analysis {
 public:
  TemporalRedundancy() : Analysis(REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) {}

  virtual ~TemporalRedundancy() = default;

  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id, GPUPatchType type);

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
  virtual void flush_now(u32 cpu_thread, const std::string &output_dir,
                         const LockableMap<u32, Cubin> &cubins,
                         redshow_record_data_callback_func record_data_callback);
  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

 private:
  // {ThreadId : {address : {<pc, value>}}}
  typedef Map<ThreadId, Map<u64, std::pair<u64, u64>>> TemporalTrace;

  // {pc : [RealPCPair]}
  typedef Map<u64, Vector<RealPCPair>> TemporalStatistics;

 private:
  void record_temporal_trace(u32 pc_views_limit, u32 mem_views_limit, PCPairs &pc_pairs,
                             PCAccessCount &pc_access_count, TemporalStatistics &temporal_stats,
                             redshow_record_data_t &record_data, u64 &kernel_temporal_count);

  void show_temporal_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count, u64 total_count,
                           TemporalStatistics &temporal_stats, bool is_thread, std::ofstream &out);

  /**
   * @brief Update the temporal trace object
   *
   * @param pc Current record's pc
   * @param tid Current record's global thread index
   * @param addr Current record's address
   * @param value How a thread accesses memory (e.g. float/int, vector/scalar)
   * @param access_kind
   * @param temporal_trace
   * @param pc_pairs
   */
  void update_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value, AccessKind access_kind,
                             TemporalTrace &temporal_trace, PCPairs &pc_pairs);

  void transform_temporal_statistics(uint32_t cubin_id, const SymbolVector &symbols,
                                     TemporalStatistics &temporal_stats);

 private:
  struct RedundancyTrace final : public Trace {
    // Temporal redundancy
    TemporalTrace read_temporal_trace;
    PCPairs read_pc_pairs;
    PCAccessCount read_pc_count;
    TemporalTrace write_temporal_trace;
    PCPairs write_pc_pairs;
    PCAccessCount write_pc_count;

    RedundancyTrace() = default;

    virtual ~RedundancyTrace() {}
  };

 private:
  static inline thread_local std::shared_ptr<RedundancyTrace> _trace;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY_H
