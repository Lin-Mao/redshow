//
// Created by find on 19-7-1.
//

#include "common_lib.h"
#include "redshow.h"


void get_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value, AccessKind access_kind,
                        TemporalTrace &temporal_trace, PCPairs &pc_pairs) {
  auto tmr_it = temporal_trace.find(tid);
  // Record current operation.
  std::map<u64, std::pair<u64, u64>> record;
  record[addr] = std::make_pair(pc, value);
  if (tmr_it == temporal_trace.end()) {
    // The trace doesn't have the thread's record
    temporal_trace[tid] = record;
  } else {
    // The trace has the thread's record
    auto m_it = tmr_it->second.find(addr);
    // m_it: {addr: <pc, value>}
    if (m_it == tmr_it->second.end()) {
      // The trace's thread record doesn't have the current addr record.
      tmr_it->second[addr] = record[addr];
    } else {
      auto prev_pc = m_it->second.first;
      auto prev_value = m_it->second.second;
      if (prev_value == value) {
        pc_pairs[pc][prev_pc][std::make_pair(prev_value, access_kind)] += 1;
      }
      m_it->second = record[addr];
    }
  }
}


void record_temporal_trace(PCPairs &pc_pairs, PCAccessCount &pc_access_count,
                           u32 pc_views_limit, u32 mem_views_limit,
                           redshow_record_data_t &record_data, TemporalStatistics &temporal_stats,
                           u64 &kernel_temporal_count) {
  // Pick top record data views
  TopViews top_views;

  // {pc1 : {pc2 : {<value, access_kind>}}}
  for (auto &to_pc_iter : pc_pairs) {
    auto to_pc = to_pc_iter.first;
    redshow_record_view_t view;
    view.pc_offset = to_pc;
    view.memory_op_id = 0;
    view.memory_id = 0;
    view.red_count = 0;
    view.access_count = pc_access_count[view.pc_offset];

    for (auto &from_pc_iter : to_pc_iter.second) {
      for (auto &val_iter : from_pc_iter.second) {
        auto val = val_iter.first.first;
        auto akind = val_iter.first.second;
        auto count = val_iter.second;
        view.red_count += count;
      }
    }

    kernel_temporal_count += view.red_count;

    if (top_views.size() < pc_views_limit) {
      top_views.push(view);
    } else {
      auto &top = top_views.top();
      if (top.red_count < view.red_count) {
        top_views.pop();
        top_views.push(view);
      }
    }
  }

  auto num_views = 0;
  // Put top record data views into record_data
  while (!top_views.empty()) {
    auto &view = top_views.top();

    if (mem_views_limit != 0) {
      TopRealPCPairs top_real_pc_pairs;
      RealPC to_pc(0, 0, view.pc_offset);
      for (auto &from_pc_iter : pc_pairs[to_pc.pc_offset]) {
        RealPC from_pc(0, 0, from_pc_iter.first);
        for (auto &val_iter : from_pc_iter.second) {
          auto val = val_iter.first.first;
          auto akind = val_iter.first.second;
          auto count = val_iter.second;

          RealPCPair real_pc_pair(to_pc, from_pc, val, akind, count, view.access_count);
          if (top_real_pc_pairs.size() < mem_views_limit) {
            top_real_pc_pairs.push(real_pc_pair);
          } else {
            auto &top = top_real_pc_pairs.top();
            if (top.red_count < real_pc_pair.red_count) {
              top_real_pc_pairs.pop();
              top_real_pc_pairs.push(real_pc_pair);
            }
          }
        }
      }

      while (top_real_pc_pairs.empty() == false) {
        auto real_pc_pair = top_real_pc_pairs.top();
        temporal_stats[view.pc_offset].push_back(real_pc_pair);
        top_real_pc_pairs.pop();
      }
    }

    record_data.views[num_views].pc_offset = view.pc_offset;
    record_data.views[num_views].red_count = view.red_count;
    record_data.views[num_views].access_count = view.access_count;
    top_views.pop();
    ++num_views;
  }
  record_data.num_views = num_views;
}


void
show_temporal_trace(u32 thread_id, u64 kernel_id, u64 total_red_count, u64 total_count,
                    TemporalStatistics &temporal_stats, bool is_read, bool is_thread) {
  using std::string;
  using std::to_string;
  using std::make_tuple;
  using std::get;
  using std::endl;
  string r = is_read ? "read" : "write";
  std::ofstream out("temporal_" + r + "_t" + to_string(thread_id) + ".csv", std::ios::app);
  if (is_thread) {
    out << "thread_id," << thread_id << endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double) total_red_count / total_count
        << endl;
  } else {
    out << "kernel_id," << kernel_id << endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double) total_red_count / total_count
        << endl;
    out
        << "cubin_id,f_function_index,f_pc_offset,t_function_index,t_pc_offest,value,data_type,vector_size,unit_size,count,rate,norm_rate"
        << endl;
    for (auto temp_iter : temporal_stats) {
      for (auto &real_pc_pair : temp_iter.second) {
        auto to_real_pc = real_pc_pair.to_pc;
        auto from_real_pc = real_pc_pair.from_pc;
        out << from_real_pc.cubin_id << "," << from_real_pc.function_index << "," << from_real_pc.pc_offset << ","
            << to_real_pc.function_index << "," << to_real_pc.pc_offset << ",";
        output_kind_value(real_pc_pair.value, real_pc_pair.access_kind, out.rdbuf(), true);
        out << "," << real_pc_pair.access_kind.to_string() << "," << real_pc_pair.red_count << "," <<
            static_cast<double>(real_pc_pair.red_count) / real_pc_pair.access_count << "," <<
            static_cast<double>(real_pc_pair.red_count) / total_count << endl;
      }
    }
  }
  out.close();
}


void get_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessKind access_kind,
                       SpatialTrace &spatial_trace) {
  spatial_trace[std::make_pair(memory_op_id, access_kind)][pc][value] += 1;
}


void record_spatial_trace(SpatialTrace &spatial_trace, PCAccessCount &pc_access_count,
                          u32 pc_views_limit, u32 mem_views_limit,
                          redshow_record_data_t &record_data, SpatialStatistics &spatial_stats,
                          u64 &kernel_spatial_count) {
  // Pick top record data views
  TopViews top_views;
  // memory_iter: {<memory_op_id, AccessKind> : {pc: {value: counter}}}
  for (auto &memory_iter : spatial_trace) {
    auto memory_op_id = memory_iter.first.first;
    // pc_iter: {pc: {value: counter}}
    for (auto &pc_iter : memory_iter.second) {
      auto pc = pc_iter.first;
      auto max_count = 0;
      // vale_iter: {value: counter}
      for (auto &val_iter : pc_iter.second) {
        auto count = val_iter.second;
        max_count = MAX2(count, max_count);
      }

      kernel_spatial_count += max_count;

      // Only record the top count of a pc
      redshow_record_view_t view;
      view.pc_offset = pc;
      view.memory_op_id = memory_op_id;
      view.memory_id = 0;
      view.red_count = max_count;
      view.access_count = pc_access_count[pc];
      if (top_views.size() < pc_views_limit) {
        top_views.push(view);
      } else {
        auto &top = top_views.top();
        if (top.red_count < view.red_count) {
          top_views.pop();
          top_views.push(view);
        }
      }
    }
  }

  // Put top record data views into record_data
  auto num_views = 0;
  while (top_views.empty() == false) {
    auto &top = top_views.top();
    auto memory_op_id = top.memory_op_id;
    auto pc = top.pc_offset;
    auto red_count = top.red_count;
    auto access_count = top.access_count;

    if (mem_views_limit != 0) {
      RealPC to_pc(0, 0, pc);
      // Update detailed memory view for each pc
      for (auto &memory_iter : spatial_trace) {
        if (memory_iter.first.first != memory_op_id) {
          continue;
        }
        auto akind = memory_iter.first.second;

        // {red_count : value}
        TopRealPCPairs top_real_pc_pairs;
        // vale_iter: {value: counter}
        for (auto &val_iter : memory_iter.second[pc]) {
          auto value = val_iter.first;
          auto count = val_iter.second;

          RealPCPair real_pc_pair(to_pc, value, akind, count, access_count);
          if (top_real_pc_pairs.size() < mem_views_limit) {
            top_real_pc_pairs.push(real_pc_pair);
          } else {
            auto &top = top_real_pc_pairs.top();
            if (top.red_count < count) {
              top_real_pc_pairs.pop();
              top_real_pc_pairs.push(real_pc_pair);
            }
          }
        }

        // {<memory_op_id> : {pc: [RealPCPair]}}
        while (top_real_pc_pairs.empty() == false) {
          auto &top = top_real_pc_pairs.top();
          spatial_stats[memory_op_id][pc].push_back(top);
          top_real_pc_pairs.pop();
        }
      }
    }

    record_data.views[num_views].pc_offset = pc;
    record_data.views[num_views].red_count = red_count;
    record_data.views[num_views].access_count = access_count;
    ++num_views;
    top_views.pop();
  }
  record_data.num_views = num_views;
}


void
show_spatial_trace(u32 thread_id, u64 kernel_id, u64 total_red_count, u64 total_count,
                   SpatialStatistics &spatial_stats, bool is_read, bool is_thread) {
  using std::endl;
  using std::to_string;
  using std::get;
  std::string r = is_read ? "read" : "write";
  std::ofstream out("spatial_" + r + "_t" + to_string(thread_id) + ".csv", std::ios::app);
  if (is_thread) {
    out << "thread_id," << kernel_id << std::endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double) total_red_count / total_count
        << endl;
  } else {
    out << "kernel_id," << kernel_id << std::endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double) total_red_count / total_count
        << endl;
    out << "memory_op_id,cubin_id,function_index,pc_offset,value,data_type,vector_size,unit_size,count,rate,norm_rate"
        << endl;
    // {memory_op_id : {pc : [RealPCPair]}}
    for (auto &spatial_iter: spatial_stats) {
      auto memory_op_id = spatial_iter.first;
      for (auto &pc_iter : spatial_iter.second) {
        for (auto &real_pc_pair : pc_iter.second) {
          auto cubin_id = real_pc_pair.to_pc.cubin_id;
          auto function_index = real_pc_pair.to_pc.function_index;
          auto pc_offset = real_pc_pair.to_pc.pc_offset;
          auto akind = real_pc_pair.access_kind;
          auto value = real_pc_pair.value;
          auto red_count = real_pc_pair.red_count;
          auto access_count = real_pc_pair.access_count;
          out << memory_op_id << "," << cubin_id << ","
              << function_index << "," << pc_offset << ",";
          output_kind_value(value, akind, out.rdbuf(), true);
          out << "," << akind.to_string() << "," << red_count << ","
              << static_cast<double>(red_count) / access_count << ","
              << static_cast<double>(red_count) / total_count << std::endl;
        }
      }
    }
  }
  out.close();
}


u64 store2basictype(u64 a, AccessKind akind, int decimal_degree_f32, int decimal_degree_f64) {
  switch (akind.data_type) {
    case REDSHOW_DATA_UNKNOWN:
      break;
    case REDSHOW_DATA_INT:
      switch (akind.unit_size) {
        case 8:
          return a & 0xffu;
        case 16:
          return a & 0xffffu;
        case 32:
          return a & 0xffffffffu;
        case 64:
          return a;
      }
      break;
    case REDSHOW_DATA_FLOAT:
      switch (akind.unit_size) {
        case 32:
          return store2float(a, decimal_degree_f32);
        case 64:
          return store2double(a, decimal_degree_f64);
      }
      break;
    default:
      break;
  }
  return a;
}


void output_kind_value(u64 a, AccessKind akind, std::streambuf *buf, bool is_signed) {
  std::ostream out(buf);
  if (akind.data_type == REDSHOW_DATA_INT) {
    if (akind.unit_size == 8) {
      if (is_signed) {
        i8 b;
        memcpy(&b, &a, sizeof(b));
        out << (int) b;
      } else {
        u8 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      }
    } else if (akind.unit_size == 16) {
      if (is_signed) {
        i16 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      } else {
        u16 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      }
    } else if (akind.unit_size == 32) {
      if (is_signed) {
        i32 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      } else {
        u32 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      }
    } else if (akind.unit_size == 64) {
      if (is_signed) {
        i64 b;
        memcpy(&b, &a, sizeof(b));
        out << b;
      } else {
        out << a;
      }
    }
  } else if (akind.data_type == REDSHOW_DATA_FLOAT) {
    // At this time, it must be float
    if (akind.unit_size == 32) {
      float b;
      memcpy(&b, &a, sizeof(b));
      out << b;
    } else if (akind.unit_size == 64) {
      double b;
      memcpy(&b, &a, sizeof(b));
      out << b;
    }
  }
}


u64 store2double(u64 a, int decimal_degree_f64) {
  u64 c = a;
  u64 bits = 52 - decimal_degree_f64;
  u64 mask = 0xffffffffffffffff << bits;
  c = c & mask;
  return c;
}


u64 store2float(u64 a, int decimal_degree_f32) {
//  valid bits are the lower 32bits.
  u32 c = a & 0xffffffffu;
  u64 bits = 23 - decimal_degree_f32;
  u64 mask = 0xffffffffffffffff << bits;
  c &= mask;
  u64 b = 0;
  memcpy(&b, &c, sizeof(c));
  return b;
}

void get_value_trace(u64 pc, u64 value, u64 memory_op_id, u64 offset, AccessKind access_kind, ValueDist &value_dist) {
  value_dist[make_pair(memory_op_id, access_kind)][offset][value] += 1;
}

bool sortByVal(const pair<u64, u64> &a,
               const pair<u64, u64> &b) {
  return (a.second > b.second);
}

/**Array_items is part of ValueDist which focuses on every offset's value. This function is going to transform offset center to value center.
 * @arg value_count: {value: count}. We only save single-value item's value
 * @arg array_items: {offset: {value: count}}
 * */
value_pattern_type_t
dense_value_pattern(ArrayItems &array_items, ItemsValueCount &value_count, AccessKind access_kind) {
// @todo one array may be considered as multiple types. What if one type is single_value_pattern but another type is not?
// @todo what if the array item is single-value at read and write with two different values?

// @todo what is the standard of dense value?
  float THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE = 0.1;
  float THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE_2 = 0.5;

// Type ArrayItems is part of ValueDist: {offset: {value: count}}
  for (auto item: array_items) {
    if (item.second.size() == 1) {
      value_count[item.second.begin()->first] += 1;
    }
  }

  std::vector<std::pair<u64, u64>> value_count_vec;
  for (auto iter: value_count) {
    value_count_vec.emplace_back(iter.first, iter.second);
  }
  sort(value_count_vec.begin(), value_count_vec.end(), sortByVal);

  array_pattern_type arr_pattern_type;
  arr_pattern_type.value_count = value_count;
//  single value pattern, redundant zeros
  if (value_count.size() == 1) {
    if (access_kind.data_type == REDSHOW_DATA_FLOAT) {
      if (access_kind.unit_size == 32) {
        uint32_t value_hex = value_count.begin()->first & 0xffffffffu;
        float b = *reinterpret_cast<float *>(&value_hex);
        if (std::abs(b) < 1e-6) {
          arr_pattern_type.value_pattern_type = VP_REDUNDANT_ZEROS;
        } else {
          arr_pattern_type.value_pattern_type = VP_SINGLE_VALUE;
        }
      } else if (access_kind.unit_size == 64) {
        double b;
        u64 cur_hex_value = value_count.begin()->first;
        memcpy(&b, &cur_hex_value, sizeof(cur_hex_value));
        if (std::abs(b) < 1e-6) {
          arr_pattern_type.value_pattern_type = VP_REDUNDANT_ZEROS;
        } else {
          arr_pattern_type.value_pattern_type = VP_SINGLE_VALUE;
        }
      }
    } else if (access_kind.data_type == REDSHOW_DATA_INT) {
      if (value_count.begin()->first == 0) {
        arr_pattern_type.value_pattern_type = VP_REDUNDANT_ZEROS;
      } else {
        arr_pattern_type.value_pattern_type = VP_SINGLE_VALUE;
      }
    }
  } else {
//    Actually array_items.size may not be the size of array. Because array_items only record accessed items.
    int threshold_number_of_items = THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE * array_items.size();
    int i=0;
    u64 sum_of_items = 0;
    for(auto iter: value_count_vec){
      sum_of_items += iter.second;
      i++;
      if ( i > threshold_number_of_items){
        break;
      }
    }
    if(sum_of_items > THRESHOLD_PERCENTAGE_OF_ARRAY_SIZE_2 * array_items.size()){
      arr_pattern_type.value_pattern_type = VP_DENSE_VALUE;
    }


  }


}