#ifndef REDSHOW_ANALYSIS_VALUE_PATTERN_H
#define REDSHOW_ANALYSIS_VALUE_PATTERN_H

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

class ValuePattern final : public Analysis {
 public:
  ValuePattern() : Analysis(REDSHOW_ANALYSIS_VALUE_PATTERN) {}

  virtual ~ValuePattern() = default;

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
  virtual void flush_now(u32 cpu_thread, const std::string &output_dir,
                         const LockableMap<u32, Cubin> &cubins,
                         redshow_record_data_callback_func record_data_callback);
  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

 private:
  struct ValueDistMemoryComp {
    bool operator()(const Memory &l, const Memory &r) const { return l.op_id < r.op_id; }
  };

  // <Offset, <Value, Count>>
  typedef Map<u64, u64> ValueCount;
  typedef Map<u64, ValueCount> ItemsValueCount;
  typedef std::map<Memory, Map<AccessKind, ItemsValueCount>, ValueDistMemoryComp> ValueDist;
  typedef std::map<Memory, Map<AccessKind, ValueCount>, ValueDistMemoryComp> ValueDistCompact;

  enum ValuePatternType {
    VP_REDUNDANT_ZEROS = 0,
    VP_SINGLE_VALUE = 1,
    VP_DENSE_VALUE = 2,
    VP_TYPE_OVERUSE = 3,
    VP_APPROXIMATE_VALUE = 4,
    VP_SILENT_STORE = 5,
    VP_SILENT_LOAD = 6,
    VP_NO_PATTERN = 7,
    VP_INAPPROPRIATE_FLOAT = 8,
    VP_STRUCTURED_PATTERN = 9,
  };
  std::map<ValuePatternType, std::string> pattern_names = {
      {VP_REDUNDANT_ZEROS, "Redundant Zeros"},
      {VP_SINGLE_VALUE, "Single Value"},
      {VP_DENSE_VALUE, "Dense Value"},
      {VP_TYPE_OVERUSE, "Type Overuse"},
      {VP_APPROXIMATE_VALUE, "Approximate Value"},
      {VP_SILENT_STORE, "Silent Store"},
      {VP_SILENT_LOAD, "Silent Load"},
      {VP_NO_PATTERN, "No Pattern"},
      {VP_INAPPROPRIATE_FLOAT, "Inappropriate Float Type"},
      {VP_STRUCTURED_PATTERN, "Structured"}};

  struct ArrayPatternInfo {
    AccessKind access_kind;
    Memory memory;
    //     <signed_leading_zero_bits, unsigned_leading_zero_bits, tail_zero_bits>
    std::tuple<int, int, int> narrow_down_to_unit_size;
    // E.g., <<10,1000>, > there are 1000 items have single value 10.
    Vector<std::pair<u64, u64>> top_value_count_vec;
    //  How many items are unique value
    int unique_item_count;
    //  The sum access number of unique value
    u64 unique_item_access_count;
    Vector<std::pair<u64, u64>> unqiue_value_count_vec;
    u64 total_access_count;
    //    for structured pattern
    double k, b, mse;
    Vector<ValuePatternType> vpts;

    ArrayPatternInfo() = default;

    uint8_t read_flag;

    ArrayPatternInfo(const AccessKind &access_kind, const Memory &memory)
        : access_kind(access_kind), memory(memory) {}
  };

  struct ValuePatternTrace final : public Trace {
    ValueDist w_value_dist;
    ValueDist r_value_dist;
    ValueDistCompact w_value_dist_compact;
    ValueDistCompact r_value_dist_compact;

    ValuePatternTrace() = default;

    virtual ~ValuePatternTrace() {}
  };

 private:
  void dense_value_pattern(ItemsValueCount &array_items, ArrayPatternInfo &array_pattern_info);

  bool approximate_value_pattern(ItemsValueCount &array_items, ArrayPatternInfo &array_pattern_info,
                                 ArrayPatternInfo &array_pattern_info_approx);

  void show_value_pattern(ArrayPatternInfo &array_pattern_info, std::ofstream &out,
                          uint8_t read_flag);

  void detect_type_overuse(std::tuple<int, int, int> &redundant_zero_bits, AccessKind &accessKind,
                           std::tuple<int, int, int> &narrow_down_to_unit_size);

  void inline check_zeros_bits(int zeros_bits, int full, int &narrow_down_to);

  bool check_exponent_and_fraction(bool &all_first_Xbits_same, u64 &all_first_Xbits,
                                   int &max_exponents_X, u64 value, AccessKind &accessKind);

  bool detect_structrued_pattern(ItemsValueCount &array_items,
                                 ArrayPatternInfo &array_pattern_info);

  bool float_no_decimal(u64 a, AccessKind &accessKind);

  void check_pattern_for_value_dist(ValueDist &value_dist, std::ofstream &out, uint8_t read_flag);

  std::tuple<int, int, int> get_redundant_zeros_bits(u64 a, AccessKind &accessKind);

  void vp_approx_level_config(redshow_approx_level_t level, int &decimal_degree_f32,
                              int &decimal_degree_f64);

 private:
  static inline thread_local std::shared_ptr<ValuePatternTrace> _trace;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_VALUE_PATTERN_H
