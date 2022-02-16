#include "analysis/value_pattern.h"

#include <algorithm>
#include <cstring>
#include <tuple>
#include <utility>

#include "common/utils.h"
#include "common/vector.h"
#include "operation/kernel.h"
#include "redshow.h"

namespace redshow {

void ValuePattern::op_callback(OperationPtr operation) {
  // Do nothing
}

// Fine-grained
void ValuePattern::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                                  GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_DEFAULT);

  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<ValuePatternTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<ValuePatternTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void ValuePattern::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void ValuePattern::block_enter(const ThreadId &thread_id) {
  // Do nothing
}

void ValuePattern::block_exit(const ThreadId &thread_id) {
  // Do nothing
}

void ValuePattern::function_call(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  // Do nothing
}

void ValuePattern::function_return(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  // Do nothing
}

void ValuePattern::unit_access(i32 kernel_id, const ThreadId &thread_id,
                               const AccessKind &access_kind, const Memory &memory, u64 pc,
                               u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  addr += index * access_kind.unit_size / 8;
  if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
    // If unknown, try each data type
    auto enum_access_kind = access_kind;
    enum_access_kind.data_type = REDSHOW_DATA_FLOAT;
    unit_access(kernel_id, thread_id, enum_access_kind, memory, pc, value, addr, index, flags);
    enum_access_kind.data_type = REDSHOW_DATA_INT;
    unit_access(kernel_id, thread_id, enum_access_kind, memory, pc, value, addr, index, flags);

    return;
  }
  //  @todo If memory usage is too high, we can limit the save of various values of one item.
  auto &r_value_dist = _trace->r_value_dist;
  auto &w_value_dist = _trace->w_value_dist;
  auto &r_value_dist_compact = _trace->r_value_dist_compact;
  auto &w_value_dist_compact = _trace->w_value_dist_compact;
  auto size = (memory.memory_range.end - memory.memory_range.start) / (access_kind.unit_size >> 3);
  auto offset = (addr - memory.memory_range.start) / (access_kind.unit_size >> 3);

  if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
    int decimal_degree_f32;
    int decimal_degree_f64;

    redshow_approx_get(&decimal_degree_f32, &decimal_degree_f64);

    if (decimal_degree_f32 == VALID_FLOAT_DIGITS) {
      // The 7th digit in float number's decimal part is partial valid, so we set the deault approx
      // level to REDSHOW_APPROX_MIN.
      vp_approx_level_config(REDSHOW_APPROX_MIN, decimal_degree_f32, decimal_degree_f64);

      if (access_kind.unit_size == 32) {
        value = value_to_float(value, decimal_degree_f32);
      } else if (access_kind.unit_size == 64) {
        value = value_to_double(value, decimal_degree_f64);
      }
    }
  }

  if (flags & GPU_PATCH_READ) {
    r_value_dist[memory][access_kind][offset][value] += 1;
  }

  if (flags & GPU_PATCH_WRITE) {
    w_value_dist[memory][access_kind][offset][value] += 1;
  }
}
void ValuePattern::flush_now(u32 cpu_thread, const std::string &output_dir, const LockableMap<u32, Cubin> &cubins, redshow_record_data_callback_func record_data_callback) {}
void ValuePattern::flush_thread(u32 cpu_thread, const std::string &output_dir,
                                const LockableMap<u32, Cubin> &cubins,
                                redshow_record_data_callback_func record_data_callback) {
  if (!this->_kernel_trace.has(cpu_thread)) {
    return;
  }

  lock();

  auto &thread_kernel_trace = this->_kernel_trace.at(cpu_thread);

  unlock();

  std::ofstream out(output_dir + "value_pattern_t" + std::to_string(cpu_thread) + ".csv");
  bool do_summary_analysis = false;
  // for all kernels
  ValueDist r_value_dist_sum;
  ValueDist w_value_dist_sum;
  for (auto &trace_iter : thread_kernel_trace) {
    auto kernel_id = trace_iter.first;
    out << "kernel id: " << kernel_id << std::endl;
    auto trace = std::dynamic_pointer_cast<ValuePatternTrace>(trace_iter.second);
    auto &r_value_dist = trace->r_value_dist;
    auto &w_value_dist = trace->w_value_dist;
    check_pattern_for_value_dist(r_value_dist, out, GPU_PATCH_READ);
    check_pattern_for_value_dist(w_value_dist, out, GPU_PATCH_WRITE);

    if (do_summary_analysis) {
      for (auto &memory_iter : r_value_dist) {
        auto &memory = memory_iter.first;
        for (auto &array_iter : memory_iter.second) {
          auto &access_kind = array_iter.first;
          auto &array_items = array_iter.second;
          for (auto offset = 0; offset < array_items.size(); ++offset) {
            auto &value_count = array_items[offset];
            for (auto &item_value_count_iter : value_count) {
              auto &item_value = item_value_count_iter.first;
              auto &item_value_count = item_value_count_iter.second;
              r_value_dist_sum[memory][access_kind][offset][item_value] += item_value_count;
            }
          }
        }
      }

      for (auto &memory_iter : w_value_dist) {
        auto &memory = memory_iter.first;
        for (auto &array_iter : memory_iter.second) {
          auto &access_kind = array_iter.first;
          auto &array_items = array_iter.second;
          for (auto offset = 0; offset < array_items.size(); ++offset) {
            auto &value_count = array_items[offset];
            for (auto &item_value_count_iter : value_count) {
              auto &item_value = item_value_count_iter.first;
              auto &item_value_count = item_value_count_iter.second;
              w_value_dist_sum[memory][access_kind][offset][item_value] += item_value_count;
            }
          }
        }
      }
    }
  }
  if (do_summary_analysis) {
    out << "================\narray pattern summary\n================" << std::endl;
    check_pattern_for_value_dist(r_value_dist_sum, out, GPU_PATCH_READ);
    check_pattern_for_value_dist(w_value_dist_sum, out, GPU_PATCH_WRITE);
  }
}

void ValuePattern::check_pattern_for_value_dist(ValueDist &value_dist, std::ofstream &out,
                                                uint8_t read_flag) {
  for (auto &memory_iter : value_dist) {
    auto &memory = memory_iter.first;
    for (auto &array_iter : memory_iter.second) {
      auto &access_kind = array_iter.first;
      auto &array_items = array_iter.second;
      ArrayPatternInfo array_pattern_info(access_kind, memory);
      dense_value_pattern(array_items, array_pattern_info);
      // Now we set approxiamte level is mid.
      ArrayPatternInfo array_pattern_info_approx(access_kind, memory);
      bool valid_approx =
          approximate_value_pattern(array_items, array_pattern_info, array_pattern_info_approx);

      show_value_pattern(array_pattern_info, out, read_flag);
      if (valid_approx) {
        out << "====  approximate ====" << std::endl;
        show_value_pattern(array_pattern_info_approx, out, read_flag);
      }
    }
  }
}

void ValuePattern::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                         redshow_record_data_callback_func record_data_callback) {}

/**This function is used to check how many significant bits are zeros.
 * @return pair<int, int> The first item is for signed and the second for unsigned.*/
std::tuple<int, int, int> ValuePattern::get_redundant_zeros_bits(u64 a, AccessKind &accessKind) {
  u64 b = a;
  u64 flag = 0x1u << (accessKind.unit_size - 1);
  char sign_bit = (a >> (accessKind.unit_size - 1)) & 0x1;
  int leading_zero_bits_signed, leading_zero_bits_unsigned, tail_zero_bits;
  a <<= 1u;
  int i;
  for (i = accessKind.unit_size - 1; i >= 0; i--) {
    if (a & flag) {
      break;
    }
    a <<= 1u;
  }
  leading_zero_bits_signed = accessKind.unit_size - 1 - i;
  leading_zero_bits_unsigned = sign_bit ? 0 : accessKind.unit_size - i;
  int j;
  for (j = 0; j < accessKind.unit_size; j++) {
    if (b & 0x1u) {
      break;
    }
    b >>= 1u;
  }
  tail_zero_bits = j;
  return std::make_tuple(leading_zero_bits_signed, leading_zero_bits_unsigned, tail_zero_bits);
}

/** Check wehther the float number has decimal part or not.*/
bool ValuePattern::float_no_decimal(u64 a, AccessKind &accessKind) {
  using std::abs;
  if (accessKind.unit_size == 32) {
    float b;
    u32 c = a & 0xffffffffu;
    memcpy(&b, &c, sizeof(c));
    int d = (int)b;
    if (abs(b - d) > 1e-6) {
      return false;
    }
  } else if (accessKind.unit_size == 64) {
    double b;
    memcpy(&b, &a, sizeof(a));
    long long d = (long long)b;
    if (abs(b - d) > 1e-14) {
      return false;
    }
  }
  return true;
}

/**@arg all_first_Xbits_same: if all first x bits are same, this value will be true.
 * @arg all_first_Xbits: store the last first Xbits of one vlaue
 * @arg Xbits: how many bits you want to check
 * @arg max_exponents_X: what is the max exponents among all values we gone through */
bool ValuePattern::check_exponent_and_fraction(bool &all_first_Xbits_same, u64 &all_first_Xbits,
                                               int &max_exponents_X, u64 value,
                                               AccessKind &accessKind) {
  u64 mask, tmp_first_Xbits, tmp_exponent;
  int tmp_abs_exponent;
  switch (accessKind.unit_size) {
    case 64:
      mask = 0xfffff00000000u;
      tmp_first_Xbits = mask & value;
      if (tmp_first_Xbits != all_first_Xbits) {
        if (all_first_Xbits != 0) {
          all_first_Xbits_same = false;
        }
        all_first_Xbits = tmp_first_Xbits;
      }
      mask = 0x7ffu;
      tmp_exponent = (value >> 52u) & mask;
      if (tmp_exponent >= 896 && tmp_exponent <= 1151) {
        tmp_abs_exponent = tmp_exponent > 1023 ? tmp_exponent - 1023 : 1023 - tmp_exponent;
        max_exponents_X =
            std::abs(max_exponents_X) > tmp_abs_exponent ? max_exponents_X : tmp_exponent - 1023;
        return true;
      } else {
        return false;
      }
      break;
    case 32:
      mask = 0x7fc000u;
      tmp_first_Xbits = mask & value;
      if (tmp_first_Xbits != all_first_Xbits) {
        if (all_first_Xbits != 0) {
          all_first_Xbits_same = false;
        }
        all_first_Xbits = tmp_first_Xbits;
      }
      mask = 0x7f8u;
      tmp_exponent = (value >> 23u) & mask;
      if (tmp_exponent >= 113 && tmp_exponent <= 142) {
        tmp_abs_exponent = tmp_exponent > 127 ? tmp_exponent - 127 : 127 - tmp_exponent;
        max_exponents_X =
            std::abs(max_exponents_X) > tmp_abs_exponent ? max_exponents_X : tmp_abs_exponent - 127;
        return true;
      } else {
        return false;
      }
      break;
  }
  return false;
}

/** @arg zeros_bits: how many leading or tail zero bits
 *  @arg full: the type size
 *  @arg narrdown_down_to: what type size the current array could narrow down to
 * */
void inline ValuePattern::check_zeros_bits(int zeros_bits, int full, int &narrow_down_to) {
  const int power2[4] = {0, 1, 2, 4};
  const int valid_int_bits[4] = {8, 16, 32, 64};
  int i;
  if (zeros_bits == full) {
    narrow_down_to = 8;
    return;
  }
  for (i = 3; i >= 0; i--) {
    if (full - 8 * power2[i] > 0 && zeros_bits < full - 8 * power2[i]) {
      break;
    }
  }
  narrow_down_to = valid_int_bits[i];
}

/**
 * @arg pair<int, int> &redundant_zero_bits how many significant bits are zeros. The first item is
 * for signed and the second for unsigned.
 *  */
void ValuePattern::detect_type_overuse(std::tuple<int, int, int> &redundant_zero_bits,
                                       AccessKind &accessKind,
                                       std::tuple<int, int, int> &narrow_down_to_unit_size) {
  using std::get;
  int narrow_down_to_unit_size_signed = accessKind.unit_size;
  int narrow_down_to_unit_size_unsigned = accessKind.unit_size;
  int narrow_down_to_unit_size_for_tail = accessKind.unit_size;
  int leading_zero_bits_signed = get<0>(redundant_zero_bits);
  int leading_zero_bits_unsigned = get<1>(redundant_zero_bits);
  int tail_zero_bits = get<2>(redundant_zero_bits);
  switch (accessKind.unit_size) {
    case 64:
      check_zeros_bits(leading_zero_bits_signed, 64, narrow_down_to_unit_size_signed);
      check_zeros_bits(leading_zero_bits_unsigned, 64, narrow_down_to_unit_size_unsigned);
      check_zeros_bits(tail_zero_bits, 64, narrow_down_to_unit_size_for_tail);
      break;
    case 32:
      check_zeros_bits(leading_zero_bits_signed, 32, narrow_down_to_unit_size_signed);
      check_zeros_bits(leading_zero_bits_unsigned, 32, narrow_down_to_unit_size_unsigned);
      check_zeros_bits(tail_zero_bits, 32, narrow_down_to_unit_size_for_tail);
      break;
    case 16:
      check_zeros_bits(leading_zero_bits_signed, 16, narrow_down_to_unit_size_signed);
      check_zeros_bits(leading_zero_bits_unsigned, 16, narrow_down_to_unit_size_unsigned);
      check_zeros_bits(tail_zero_bits, 16, narrow_down_to_unit_size_for_tail);
      break;
  }
  narrow_down_to_unit_size =
      std::make_tuple(narrow_down_to_unit_size_signed, narrow_down_to_unit_size_unsigned,
                      narrow_down_to_unit_size_for_tail);
}  // namespace redshow

void ValuePattern::dense_value_pattern(ItemsValueCount &array_items,
                                       ArrayPatternInfo &array_pattern_info) {
  using std::get;
  using std::make_tuple;
  using std::pair;
  using std::tuple;
  auto &access_kind = array_pattern_info.access_kind;
  u64 memory_size = array_pattern_info.memory.len;
  //    number of accessed items
  auto number_of_items = array_items.size();
  //  the following variables will be updated.
  auto &top_value_count_vec = array_pattern_info.top_value_count_vec;
  int unique_item_count = 0;
  auto &vpts = array_pattern_info.vpts;
  auto &narrow_down_to_unit_size = array_pattern_info.narrow_down_to_unit_size;
  auto &unique_value_count_vec = array_pattern_info.unqiue_value_count_vec;
  // special memory array
  if (memory_size == 0) {
    vpts.emplace_back(VP_NO_PATTERN);
    return;
  }
  u64 total_access_count = 0;
  u64 total_unique_item_access_count = 0;
  float THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE = 0.1;
  float THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE_2 = 0.5;
  int TOP_NUM_VALUE = 10;

  ValueCount unique_value_count;
  bool inappropriate_float_type = true;
  //     <signed_leading_zero_bits, unsigned_leading_zero_bits, tail_zero_bits>
  tuple<int, int, int> redundant_zero_bits =
      make_tuple(access_kind.unit_size, access_kind.unit_size, access_kind.unit_size);
  bool all_first_20bits_double_same = true;
  bool all_first_9bits_float_same = true;
  u64 last_first_20bits_double = 0;
  u64 last_first_9bits_float = 0;
  const int MAX_DOUBLE_EXPONENT = 127;
  const int MAX_FLOAT_EXPONENT = 14;
  int max_exponents_double = 0;
  int max_exponents_float = 0;
  bool possible_float_type_overuse = true;
  // Type ArrayItems is part of ValueDist: {offset: {value: count}}
  for (auto temp_item_value_count_w_i : array_items) {
    auto temp_item_value_count = temp_item_value_count_w_i.second;
    if (access_kind.data_type == REDSHOW_DATA_INT) {
      for (auto temp_value : temp_item_value_count) {
        //          int type overuse
        auto temp_redundat_zero_bits = get_redundant_zeros_bits(temp_value.first, access_kind);
        redundant_zero_bits =
            make_tuple(std::min(get<0>(redundant_zero_bits), get<0>(temp_redundat_zero_bits)),
                       std::min(get<1>(redundant_zero_bits), get<1>(temp_redundat_zero_bits)),
                       std::min(get<2>(redundant_zero_bits), get<2>(temp_redundat_zero_bits)));
      }
    } else if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
      for (auto temp_value : temp_item_value_count) {
        if (inappropriate_float_type && !float_no_decimal(temp_value.first, access_kind)) {
          inappropriate_float_type = false;
          break;
        }
      }
      if (possible_float_type_overuse) {
        for (auto temp_value : temp_item_value_count) {
          //          float type overuse
          if (access_kind.unit_size == 64) {
            //            float exponent range : -127 ~ 128
            if (max_exponents_double >= -127 && max_exponents_double <= 128) {
              possible_float_type_overuse = check_exponent_and_fraction(
                  all_first_20bits_double_same, last_first_20bits_double, max_exponents_double,
                  temp_value.first, access_kind);
              if (not possible_float_type_overuse) break;
            }
          } else if (access_kind.unit_size == 32) {
            //            IEEE 754 half exponent range: -14 ~ 15
            if (max_exponents_double >= -14 && max_exponents_double <= 15) {
              possible_float_type_overuse =
                  check_exponent_and_fraction(all_first_9bits_float_same, last_first_9bits_float,
                                              max_exponents_double, temp_value.first, access_kind);
              if (not possible_float_type_overuse) break;
            }
          }
        }
      }
    }
    if (temp_item_value_count.size() == 1) {
      unique_value_count[temp_item_value_count.begin()->first] += 1;
      unique_item_count++;
    }
    for (auto item_value_count_one : temp_item_value_count) {
      total_access_count += item_value_count_one.second;
      if (temp_item_value_count.size() == 1)
        total_unique_item_access_count += item_value_count_one.second;
    }
  }
  array_pattern_info.total_access_count = total_access_count;
  array_pattern_info.unique_item_count = unique_item_count;
  array_pattern_info.unique_item_access_count = total_unique_item_access_count;
  if (detect_structrued_pattern(array_items, array_pattern_info)) {
    vpts.emplace_back(VP_STRUCTURED_PATTERN);
  }
  if (access_kind.data_type == REDSHOW_DATA_FLOAT && inappropriate_float_type) {
    vpts.emplace_back(VP_INAPPROPRIATE_FLOAT);
  }
  if (access_kind.data_type == REDSHOW_DATA_INT) {
    detect_type_overuse(redundant_zero_bits, access_kind, narrow_down_to_unit_size);
    if (access_kind.unit_size != get<0>(narrow_down_to_unit_size) ||
        access_kind.unit_size != get<1>(narrow_down_to_unit_size) ||
        access_kind.unit_size != get<2>(narrow_down_to_unit_size)) {
      //      it is type overuse pattern
      vpts.emplace_back(VP_TYPE_OVERUSE);
    }
  }

  for (auto iter : unique_value_count) {
    unique_value_count_vec.emplace_back(iter.first, iter.second);
  }
  std::sort(unique_value_count_vec.begin(), unique_value_count_vec.end(),
            [](auto &l, auto &r) { return l.second > r.second; });

  for (int i = 0; i < std::min((size_t)TOP_NUM_VALUE, unique_value_count_vec.size()); i++) {
    top_value_count_vec.emplace_back(unique_value_count_vec[i]);
  }
  ValuePatternType vpt = VP_NO_PATTERN;
  //  single value pattern, redundant zeros
  if (unique_value_count.size() == 1) {
    if (total_access_count == total_unique_item_access_count) {
      if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
        if (access_kind.unit_size == 32) {
          uint32_t value_hex = unique_value_count.begin()->first & 0xffffffffu;
          float b = *reinterpret_cast<float *>(&value_hex);
          if (std::abs(b) < 1e-6) {
            vpt = VP_REDUNDANT_ZEROS;
          } else {
            vpt = VP_SINGLE_VALUE;
          }
        } else if (access_kind.unit_size == 64) {
          double b;
          u64 cur_hex_value = unique_value_count.begin()->first;
          memcpy(&b, &cur_hex_value, sizeof(cur_hex_value));
          if (std::abs(b) < 1e-14) {
            vpt = VP_REDUNDANT_ZEROS;
          } else {
            vpt = VP_SINGLE_VALUE;
          }
        }
      } else if (access_kind.data_type == REDSHOW_DATA_INT) {
        if (unique_value_count.begin()->first == 0) {
          vpt = VP_REDUNDANT_ZEROS;
        } else {
          vpt = VP_SINGLE_VALUE;
        }
      }
    }
  } else {
    if (unique_item_count >= THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE_2 * number_of_items) {
      if (unique_value_count_vec.size() <= THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE * number_of_items) {
        vpt = VP_DENSE_VALUE;
      }
    }
  }
  //    float type overuse
  if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
    if (access_kind.unit_size == 32) {
      if (((vpt == VP_SINGLE_VALUE or vpt == VP_REDUNDANT_ZEROS) ||
           (not all_first_9bits_float_same)) &&
          max_exponents_float <= MAX_FLOAT_EXPONENT) {
        narrow_down_to_unit_size = make_tuple(16, 32, 32);
        vpts.emplace_back(VP_TYPE_OVERUSE);
      }
    } else if (access_kind.unit_size == 64) {
      if (((vpt == VP_SINGLE_VALUE or vpt == VP_REDUNDANT_ZEROS) ||
           (not all_first_20bits_double_same)) &&
          max_exponents_double <= MAX_DOUBLE_EXPONENT) {
        narrow_down_to_unit_size = make_tuple(32, 64, 64);
        vpts.emplace_back(VP_TYPE_OVERUSE);
      }
    }
  }
  if (vpts.size() != 0 && vpt == VP_NO_PATTERN) {
  } else {
    vpts.emplace_back(vpt);
  }
}

/**
 * @arg array_items: [offset: {value: count}] It is an array to save the value distribution of the
 * target array. */
bool ValuePattern::approximate_value_pattern(ItemsValueCount &array_items,
                                             ArrayPatternInfo &array_pattern_info,
                                             ArrayPatternInfo &array_pattern_info_approx) {
  auto &access_kind = array_pattern_info.access_kind;
  auto &memory = array_pattern_info.memory;
  if (access_kind.data_type != REDSHOW_DATA_FLOAT) {
    return false;
  }
  int decimal_degree_f32, decimal_degree_f64;
  vp_approx_level_config(REDSHOW_APPROX_MAX, decimal_degree_f32, decimal_degree_f64);
  //    auto number_of_items = array_pattern_info.memory.len / (access_kind.unit_size >> 3);
  auto number_of_items = array_items.size();
  ItemsValueCount array_items_approx;
  //    for (auto i = 0; i < number_of_items; ++i) {
  for (auto one_item_value_count_w_i : array_items) {
    auto index = one_item_value_count_w_i.first;
    auto one_item_value_count = one_item_value_count_w_i.second;
    for (auto one_value_count : one_item_value_count) {
      if (access_kind.unit_size == 32) {
        u64 new_value = value_to_float(one_value_count.first, decimal_degree_f32);
        array_items_approx[index][new_value] += one_value_count.second;
      } else if (access_kind.unit_size == 64) {
        u64 new_value = value_to_double(one_value_count.first, decimal_degree_f64);
        array_items_approx[index][new_value] += one_value_count.second;
      }
    }
  }
  dense_value_pattern(array_items_approx, array_pattern_info_approx);
  auto new_vpts = array_pattern_info_approx.vpts;
  auto vpts = array_pattern_info.vpts;
  bool valid_approx = false;
  if (new_vpts.size() > 0) {
    if (new_vpts.size() != vpts.size()) {
      valid_approx = true;
    } else {
      std::sort(new_vpts.begin(), new_vpts.end());
      std::sort(vpts.begin(), vpts.end());
      for (int i = 0; i < new_vpts.size(); i++) {
        if (vpts[i] != new_vpts[i]) {
          valid_approx = true;
          break;
        }
      }
    }
  }
  return valid_approx;
}

void ValuePattern::show_value_pattern(ArrayPatternInfo &array_pattern_info, std::ofstream &out,
                                      uint8_t read_flag) {
  using std::endl;
  using std::get;
  auto &memory = array_pattern_info.memory;
  int unique_item_count = array_pattern_info.unique_item_count;
  auto value_count_vec = array_pattern_info.unqiue_value_count_vec;
  auto access_kind = array_pattern_info.access_kind;
  auto memory_size = array_pattern_info.memory.len;
  auto vpts = array_pattern_info.vpts;
  auto narrow_down_to_unit_size = array_pattern_info.narrow_down_to_unit_size;
  auto top_value_count_vec = array_pattern_info.top_value_count_vec;
  std::string read_write = read_flag == GPU_PATCH_READ ? "Read" : "Write";
  out << "array id: " << memory.ctx_id << ", memory size " << memory_size << ", value type "
      << access_kind.to_string() << " " << read_write << endl;
  out << "total access count: " << array_pattern_info.total_access_count << endl;
  out << "unique item count: " << unique_item_count << endl;
  out << "unqiue item value count: " << value_count_vec.size() << endl;
  out << "unqiue item access count: " << array_pattern_info.unique_item_access_count << endl;
  out << "pattern type:\n";
  if (vpts.size() == 0) vpts.emplace_back(VP_NO_PATTERN);
  for (auto a_vpt : vpts) {
    out << " * " << pattern_names[a_vpt] << "\t";
    AccessKind temp_a;
    auto narrow_down_to_unit_size_signed = get<0>(narrow_down_to_unit_size);
    auto narrow_down_to_unit_size_unsigned = get<1>(narrow_down_to_unit_size);
    auto narrow_down_to_unit_size_for_tail = get<2>(narrow_down_to_unit_size);
    switch (a_vpt) {
      case VP_TYPE_OVERUSE:
        if (access_kind.unit_size != narrow_down_to_unit_size_signed) {
          temp_a.data_type = access_kind.data_type;
          temp_a.unit_size = narrow_down_to_unit_size_signed;
          temp_a.vec_size = temp_a.unit_size * (access_kind.vec_size / access_kind.unit_size);
          out << "signed: " << access_kind.to_string() << " --> " << temp_a.to_string() << "\t";
        }
        if (access_kind.unit_size != narrow_down_to_unit_size_unsigned) {
          temp_a.data_type = access_kind.data_type;
          temp_a.unit_size = narrow_down_to_unit_size_unsigned;
          temp_a.vec_size = temp_a.unit_size * (access_kind.vec_size / access_kind.unit_size);
          out << "unsigned: " << access_kind.to_string() << " --> " << temp_a.to_string() << "\t";
        }
        if (access_kind.unit_size != narrow_down_to_unit_size_for_tail) {
          temp_a.data_type = access_kind.data_type;
          temp_a.unit_size = narrow_down_to_unit_size_for_tail;
          temp_a.vec_size = temp_a.unit_size * (access_kind.vec_size / access_kind.unit_size);
          out << "cased by tail zeros: " << access_kind.to_string() << " --> "
              << temp_a.to_string();
        }
        break;
      case VP_INAPPROPRIATE_FLOAT:
        temp_a.data_type = REDSHOW_DATA_INT;
        temp_a.unit_size = access_kind.unit_size;
        temp_a.vec_size = access_kind.vec_size;
        out << access_kind.to_string() << " --> " << temp_a.to_string();
        break;
      case VP_STRUCTURED_PATTERN:
        out << "y = " << array_pattern_info.k << "x + " << array_pattern_info.b
            << " mse: " << array_pattern_info.mse;
        break;
    }
    out << endl;
  }
  if (top_value_count_vec.size() != 0) {
    out << "TOP unqiue value\tcount" << endl;
    for (auto item : top_value_count_vec) {
      out << access_kind.value_to_string(item.first, true) << "\t" << item.second << endl;
    }
  }
  out << endl;
}

bool ValuePattern::detect_structrued_pattern(ItemsValueCount &array_items,
                                             ArrayPatternInfo &array_pattern_info) {
  //  All of the items are unique items.
  auto &access_kind = array_pattern_info.access_kind;
  //    u64 number_of_items = array_pattern_info.memory.len / (access_kind.unit_size >> 3);
  u64 number_of_items = array_items.size();
  //  Ignore the 0th and the last item.
  if (array_pattern_info.unique_item_count == number_of_items && number_of_items >= 3) {
    double sum_x = 0;
    double sum_y_f = 0;
    long long sum_y_i = 0;
    double avg_y = 0;
    for (auto temp_item_value_count_wi : array_items) {
      sum_x += temp_item_value_count_wi.first;
    }
    double avg_x = sum_x / number_of_items;
    if (access_kind.data_type == REDSHOW_DATA_INT) {
      for (auto temp_item_value_count_wi : array_items) {
        sum_y_i += temp_item_value_count_wi.second.begin()->first;
      }
      avg_y = (double)sum_y_i / number_of_items;
    } else if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
      float a;
      double b;
      for (auto temp_item_value_count_wi : array_items) {
        auto &temp_item_value_count = temp_item_value_count_wi.second;
        if (access_kind.unit_size == 32) {
          u32 c = temp_item_value_count.begin()->first & 0xffffffffu;
          memcpy(&a, &c, sizeof(c));
          sum_y_f += a;
        } else if (access_kind.unit_size == 64) {
          u64 c = temp_item_value_count.begin()->first & 0xffffffffu;
          memcpy(&b, &c, sizeof(c));
          sum_y_f += b;
        }
      }
      avg_y = sum_y_f / number_of_items;
    }
    double sum1 = 0, sum2 = 0, mse = 0;
    for (auto temp_item_value_count_wi : array_items) {
      auto i = temp_item_value_count_wi.first;
      sum1 += (i - avg_x) * (temp_item_value_count_wi.second.begin()->first - avg_y);
      sum2 += (i - avg_x) * (i - avg_x);
    }
    double k = sum1 / sum2;
    double b = avg_y - k * avg_x;
    double threshold = 0;
    //      calculate precision of our predictor
    for (auto temp_item_value_count_wi : array_items) {
      double y_p = k * temp_item_value_count_wi.first + b;
      double temp_t = y_p - temp_item_value_count_wi.second.begin()->first;
      mse += temp_t * temp_t;
      threshold += (0.1 * temp_t) * (0.1 * temp_t);
    }
    mse = mse / number_of_items;
    threshold = threshold / number_of_items;
    if (std::abs(mse - threshold) < 1e-3) {
      array_pattern_info.k = k;
      array_pattern_info.b = b;
      array_pattern_info.mse = mse;
      return true;
    } else
      return false;
  }
  return false;
}

void ValuePattern::vp_approx_level_config(redshow_approx_level_t level, int &decimal_degree_f32,
                                          int &decimal_degree_f64) {
  switch (level) {
    case REDSHOW_APPROX_NONE:
      decimal_degree_f32 = VALID_FLOAT_DIGITS;
      decimal_degree_f64 = VALID_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MIN:
      decimal_degree_f32 = MIN_FLOAT_DIGITS;
      decimal_degree_f64 = MIN_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_LOW:
      decimal_degree_f32 = LOW_FLOAT_DIGITS;
      decimal_degree_f64 = LOW_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MID:
      decimal_degree_f32 = MID_FLOAT_DIGITS;
      decimal_degree_f64 = MID_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_HIGH:
      decimal_degree_f32 = HIGH_FLOAT_DIGITS;
      decimal_degree_f64 = HIGH_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MAX:
      decimal_degree_f32 = MAX_FLOAT_DIGITS;
      decimal_degree_f64 = MAX_DOUBLE_DIGITS;
      break;
    default:
      decimal_degree_f32 = MIN_FLOAT_DIGITS;
      decimal_degree_f64 = MIN_DOUBLE_DIGITS;
      break;
  }
}

}  // namespace redshow
