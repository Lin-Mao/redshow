//
// Created by find on 19-7-1.
//

#ifndef REDSHOW_ANALYSIS_REDUNDANCY_H
#define REDSHOW_ANALYSIS_REDUNDANCY_H

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
#include "redshow.h"
#include "binutils/instruction.h"
#include "common/utils.h"

namespace redshow {

class Redundancy final : Analysis {
 public:
  Redundancy() : Analysis(REDSHOW_ANALYSIS_REDUNDANCY) {}

  // Coarse-grained
  virtual void op_callback(Operation &operation);

  virtual void analysis_begin(i32 kernel_id);

  virtual void analysis_end(i32 kernel_id);

  virtual void block_enter(i32 kernel_id, const ThreadId &thread_id);

  virtual void block_exit(i32 kernel_id, const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           u64 memory_op_id, u64 pc, u64 value, u64 addr, u32 stride, u32 index,
                           bool read);

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const Map<u32, Cubin> &cubins,
                            redshow_record_data_callback_func *record_data_callback) = 0;

  virtual void flush(const Map<u32, Cubin> &cubins, const std::string &output_dir,
                     const std::vector<OperationPtr> operations,
                     redshow_record_data_callback_func *record_data_callback) = 0;

 private:
  struct RealPC {
    u32 cubin_id;
    u32 function_index;
    u64 pc_offset;

    RealPC() = default;

    RealPC(u32 cubin_id, u32 function_index, u64 pc_offset)
        : cubin_id(cubin_id), function_index(function_index), pc_offset(pc_offset) {}

    bool operator<(const RealPC &other) const {
      if (this->cubin_id == other.cubin_id) {
        if (this->function_index == other.function_index) {
          return this->pc_offset < other.pc_offset;
        }
        return this->function_index < other.function_index;
      }
      return this->cubin_id < other.cubin_id;
    }
  };

  struct RealPCPair {
    RealPC to_pc;
    RealPC from_pc;
    u64 value;
    AccessKind access_kind;
    u64 red_count;
    u64 access_count;

    RealPCPair() = default;

    RealPCPair(RealPC &to_pc, u64 value, AccessKind &access_kind, u64 red_count,
               u64 access_count)
        : to_pc(to_pc),
          value(value),
          access_kind(access_kind),
          red_count(red_count),
          access_count(access_count) {}

    RealPCPair(RealPC &to_pc, RealPC &from_pc, u64 value, AccessKind &access_kind,
               u64 red_count, u64 access_count)
        : to_pc(to_pc),
          from_pc(from_pc),
          value(value),
          access_kind(access_kind),
          red_count(red_count),
          access_count(access_count) {}
  };

  // {<memory_op_id, AccessKind> : {pc: {value: count}}}
  typedef Map<std::pair<u64, AccessKind>, Map<u64, Map<u64, u64>>> SpatialTrace;

  // {<memory_op_id> : {pc: [RealPCPair]}}
  typedef Map<u64, Map<u64, Vector<RealPCPair>>> SpatialStatistics;

  // {ThreadId : {address : {<pc, value>}}}
  typedef Map<ThreadId, Map<u64, std::pair<u64, u64>>> TemporalTrace;

  // {pc : [RealPCPair]}
  typedef Map<u64, Vector<RealPCPair>> TemporalStatistics;

  // {pc1 : {pc2 : {<value, AccessKind> : count}}}
  typedef Map<u64, Map<u64, Map<std::pair<u64, AccessKind>, u64>>> PCPairs;

  // {pc: access_count}
  typedef Map<u64, u64> PCAccessCount;

  struct CompareRealPCPair {
    bool operator()(RealPCPair const &r1, RealPCPair const &r2) {
      return r1.red_count > r2.red_count;
    }
  };

  typedef std::priority_queue<RealPCPair, Vector<RealPCPair>, CompareRealPCPair>
      TopRealPCPairs;

  struct CompareView {
    bool operator()(redshow_record_view_t const &r1, redshow_record_view_t const &r2) {
      return r1.red_count > r2.red_count;
    }
  };

  typedef std::priority_queue<redshow_record_view_t, Vector<redshow_record_view_t>,
                              CompareView>
      TopViews;

  struct Trace {
    // Spatial redundancy
    SpatialTrace read_spatial_trace;
    SpatialTrace write_spatial_trace;

    // Temporal redundancy
    TemporalTrace read_temporal_trace;
    PCPairs read_pc_pairs;
    PCAccessCount read_pc_count;
    TemporalTrace write_temporal_trace;
    PCPairs write_pc_pairs;
    PCAccessCount write_pc_count;
  };

 private:
  void record_spatial_trace(u32 cpu_thread, u32 kernel_id, u32 pc_views_limit, u32 mem_views_limit,
                            redshow_record_data_t &record_data, u64 &kernel_spatial_count);

  void show_spatial_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count, u64 total_count,
                          bool is_read, bool is_thread);

  void record_temporal_trace(u32 cpu_thread, i32 kernel_id, u32 pc_views_limit, u32 mem_views_limit,
                             redshow_record_data_t &record_data, u64 &kernel_temporal_count);

  void show_temporal_trace(u32 thread_id, i32 kernel_id, u64 total_red_count, u64 total_count,
                           bool is_read, bool is_thread);

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
  void update_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value,
                             AccessKind access_kind, TemporalTrace &temporal_trace,
                             PCPairs &pc_pairs);


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


 private:
  thread_local std::shared_ptr<Trace> _trace;
  LockableMap<u32, Map<i32, Trace>> _kernel_trace;
};

}  // namespace redshow

#endif  // REDSHOW_REDUNDANCY_H
