#ifndef REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY_H
#define REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY_H

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

class SpatialRedundancy final : public Analysis {
 public:
  SpatialRedundancy() : Analysis(REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) {}

  virtual ~SpatialRedundancy() = default;

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

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

 private:
  // {<memory_op_id, AccessKind> : {pc: {value: count}}}
  typedef Map<std::pair<u64, AccessKind>, Map<u64, Map<u64, u64>>> SpatialTrace;

  // {<memory_op_id> : {pc: [RealPCPair]}}
  typedef Map<u64, Map<u64, Vector<RealPCPair>>> SpatialStatistics;

 private:
  void record_spatial_trace(u32 pc_views_limit, u32 mem_views_limit, SpatialTrace &spatial_trace,
                            PCAccessCount &pc_access_count, SpatialStatistics &spatial_stats,
                            redshow_record_data_t &record_data, u64 &kernel_spatial_count);

  void show_spatial_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count, u64 total_count,
                          SpatialStatistics &spatial_stats, bool is_thread, std::ofstream &out);

  /**
   * @brief Update the spatial trace object
   *
   * @param pc Current record's pc
   * @param value Current record's basic value
   * @param memory_op_id Current record's memory identifier
   * @param access_kind How a thread accesses memory (e.g. float/int, vector/scalar)
   * @param spatial_trace
   */
  void update_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessKind access_kind,
                            SpatialTrace &spatial_trace);

  void transform_spatial_statistics(u32 cubin_id, const SymbolVector &symbols,
                                    SpatialStatistics &spatial_stats);

 private:
  struct RedundancyTrace final : public Trace {
    // Spatial redundancy
    SpatialTrace read_spatial_trace;
    SpatialTrace write_spatial_trace;

    PCAccessCount read_pc_count;
    PCAccessCount write_pc_count;

    RedundancyTrace() = default;

    virtual ~RedundancyTrace() {}
  };

 private:
  static inline thread_local std::shared_ptr<RedundancyTrace> _trace;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY_H
