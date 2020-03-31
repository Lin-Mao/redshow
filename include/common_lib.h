//
// Created by find on 19-7-1.
//

#ifndef REDSHOW_COMMON_LIB_H
#define REDSHOW_COMMON_LIB_H

#include <string>
#include <regex>
#include <list>
#include <map>
#include <numeric>
#include <fstream>
#include <set>
#include <regex>
#include <queue>

#include <tuple>
#include <algorithm>

#include "instruction.h"
#include "utils.h"
#include "redshow.h"

/*
 * Data type definition
 */

struct ThreadId {
  u32 flat_block_id;
  u32 flat_thread_id;

  bool operator<(const ThreadId &o) const {
    return (this->flat_block_id < o.flat_block_id) ||
           (this->flat_block_id == o.flat_block_id && this->flat_thread_id < o.flat_thread_id);
  }

  bool operator==(const ThreadId &o) const {
    return this->flat_thread_id == o.flat_thread_id &&
           this->flat_block_id == o.flat_block_id;
  }
};

struct RealPC {
  u32 cubin_id;
  u32 function_index;
  u64 pc_offset;

  RealPC(u32 cubin_id, u32 function_index, u64 pc_offset) :
    cubin_id(cubin_id), function_index(function_index), pc_offset(pc_offset) {}

  RealPC() : RealPC(0, 0, 0) {}

  bool operator < (const RealPC &other) const {
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

  RealPCPair(RealPC &to_pc, u64 value, AccessKind &access_kind, u64 red_count, u64 access_count) :
    to_pc(to_pc), value(value), access_kind(access_kind), red_count(red_count), access_count(access_count) {}

  RealPCPair(RealPC &to_pc, RealPC &from_pc, u64 value, AccessKind &access_kind, u64 red_count, u64 access_count) :
    to_pc(to_pc), from_pc(from_pc), value(value), access_kind(access_kind), red_count(red_count), access_count(access_count) {}
};

// {<memory_op_id, AccessKind> : {pc: {value: count}}}
typedef std::map<std::pair<u64, AccessKind>, std::map<u64, std::map<u64, u64>>> SpatialTrace;

// {<memory_op_id> : {pc: [RealPCPair]}}
typedef std::map<u64, std::map<u64, std::vector<RealPCPair>>> SpatialStatistics;

// {ThreadId : {address : {<pc, value>}}}
typedef std::map<ThreadId, std::map<u64, std::pair<u64, u64>>> TemporalTrace;

// {pc : [RealPCPair]}
typedef std::map<u64, std::vector<RealPCPair>> TemporalStatistics;

// {pc1 : {pc2 : {<value, AccessKind> : count}}}
typedef std::map<u64, std::map<u64, std::map<std::pair<u64, AccessKind>, u64>>> PCPairs;

// {pc: access_count}
typedef std::map<u64, u64> PCAccessCount;

struct CompareRealPCPair {
  bool operator()(RealPCPair const &r1, RealPCPair const &r2) {
    return r1.red_count > r2.red_count;
  }
};

typedef std::priority_queue<RealPCPair, std::vector<RealPCPair>, CompareRealPCPair> TopRealPCPairs;

struct CompareView {
  bool operator()(redshow_record_view_t const &r1, redshow_record_view_t const &r2) {
    return r1.red_count > r2.red_count;
  }
};

typedef std::priority_queue<redshow_record_view_t, std::vector<redshow_record_view_t>, CompareView> TopViews;

/*
 * Interface:
 *
 * Each analysis type has three methods
 * 1. get_<analysis_type>
 * Analyze a segment of trace
 *
 * 2. record_<analysis_type>
 * Attribute high level analysis result to runtime
 *
 * 3. show_<analysis_type>
 * Debug/output detailed analysis result to file
 */

/*
 * Analyze temporal trace
 *
 * pc:
 * Current record's pc
 *
 * tid:
 * Current record's global thread index
 *
 * addr:
 * Current record's address
 *
 * access_type:
 * How a thread accesses memory (e.g. float/int, vector/scalar)
 *
 * temporal_trace:
 * {ThreadId : {address : {<pc, value>}}}
 *
 * pc_pairs:
 * {pc1 : {pc2 : {<value, kind>}}}
 */
void get_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value, AccessKind access_kind,
                        TemporalTrace &temporal_trace, PCPairs &pc_pairs);

/*
 * Record frequent temporal records
 *
 * pc_pairs:
 * {pc1 : {pc2 : {<value, kind>}}}
 *
 * record_data:
 * Data returned to the runtime
 *
 * num_views_limit:
 * Number of entries the runtime needs to know
 */
void record_temporal_trace(PCPairs &pc_pairs, PCAccessCount &pc_access_count,
                           u32 pc_views_limit, u32 mem_views_limit,
                           redshow_record_data_t &record_data, TemporalStatistics &temporal_stats,
                           u64 &kernel_red_count, u64 &kernel_access_count);

void show_temporal_trace(u32 thread_id, u64 kernel_id, TemporalStatistics &temporal_stats, bool is_read,
                         u64 kernel_red_count, u64 kernel_total_count);

/*
 * Analyze spatial trace
 *
 * pc:
 * Current record's pc
 *
 * memory_op_id:
 * Current record's memory identifier
 *
 * access_kind:
 * How a thread accesses memory (e.g. float/int, vector/scalar)
 *
 * spatial_trace:
 * {<memory_op_id, AccessKind> : {pc: {value: count}}}
 *
 */
void get_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessKind access_kind,
                       SpatialTrace &spatial_trace);

/*
 * Record frequent spatial records
 *
 * spatial_trace:
 * {<memory_op_id, AccessKind> : {pc: {value: count}}}
 *
 * record_data:
 * Data returned to the runtime
 *
 * num_views_limit:
 * Number of entries the runtime needs to know
 */
void record_spatial_trace(SpatialTrace &spatial_trace, PCAccessCount &pc_access_count,
                          u32 pc_views_limit, u32 mem_views_limit,
                          redshow_record_data_t &record_data, SpatialStatistics &spatial_stats);

/**
 * Write array's value statistic data into files.
 * @arg thread_id: cpu thread id
 * @arg spatial_statistic: {memory_op_id: {value: count}}
 * @arg num_views_limit: numer of entries will be written into files.
 * @arg is_read: the spatial_statistic is for reading or writing accesses.
 * */
void show_spatial_trace(u32 thread_id, u64 kernel_id, SpatialStatistics &spatial_stats, bool is_read);

/**
 * Use decimal_degree_f32 bits to cut the valid floating number bits.
 * @arg decimal_degree_f32: the valid bits. The floating numbers have 23-bit fractions.
 * */
u64 store2float(u64 a, int decimal_degree_f32);

/**
 * @arg decimal_degree_f64: the valid bits. The float64 numbers have 52-bit fractions.
 * */
u64 store2double(u64 a, int decimal_degree_f64);

/**
 * Change raw data to formatted value.
 * */
u64 store2basictype(u64 a, AccessKind akind, int decimal_degree_f32, int decimal_degree_f64);

void output_kind_value(u64 a, AccessKind akind, std::streambuf *buf, bool is_signed);

#endif  // REDSHOW_COMMON_LIB_H


