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
        pc_pairs[prev_pc][pc][std::make_pair(prev_value, access_kind)] += 1;
      }
      m_it->second = record[addr];
    }
  }
}


void record_temporal_trace(PCPairs &pc_pairs, PCAccessSum &pc_access_sum, redshow_record_data_t &record_data,
                           uint32_t num_views_limit, uint64_t &kernel_red_count, uint64_t &kernel_count,
                           std::vector<TopPair> &top_pairs) {
  // Pick top record data views
  TopViews top_views;
  TopPairs temp_top_pairs;
  // {pc1 : {pc2 : {<value, kind>}}}
  for (auto &from_pc_iter : pc_pairs) {
    for (auto &to_pc_iter : from_pc_iter.second) {
      auto to_pc = to_pc_iter.first;
      // {<value, kind> : count}
      for (auto &val_iter : to_pc_iter.second) {
        auto val = val_iter.first.first;
        auto akind = val_iter.first.second;
        auto count = val_iter.second;
        redshow_record_view_t view;
        view.pc_offset = to_pc;
        view.memory_id = 0;
        view.count = count;
        kernel_red_count += count;
        view.access_sum_count = pc_access_sum[to_pc];
        kernel_count += view.access_sum_count;
        RealPC f_pc;
        f_pc.function_index = 0;
        f_pc.cubin_id = 0;
        f_pc.pc = from_pc_iter.first;
        RealPC t_pc;
        t_pc.function_index = 0;
        t_pc.cubin_id = 0;
        t_pc.pc = to_pc;
        TopPair apair;
        apair.from_pc = f_pc;
        apair.to_pc = t_pc;
        apair.value = val;
        apair.kind = akind;
        apair.count = count;
        if (top_views.size() < num_views_limit) {
          top_views.push(view);
          temp_top_pairs.push(apair);
        } else {
          auto &top = top_views.top();
          if (top.count < view.count) {
            top_views.pop();
            top_views.push(view);
            temp_top_pairs.pop();
            temp_top_pairs.push(apair);
          }
        }
      }
    }
  }

  auto num_views = 0;
  // Put top record data views into record_data
  while (!top_views.empty()) {
    auto &view = top_views.top();
    record_data.views[num_views++] = view;
    top_views.pop();
  }
  while (!temp_top_pairs.empty()) {
    auto &pair = temp_top_pairs.top();
    top_pairs.push_back(pair);
    temp_top_pairs.pop();
  }
  record_data.num_views = num_views;
}


void
show_temporal_trace(std::vector<Symbol> &symbols, u64 kernel_id, PCAccessSum &pc_access_sum,
                    bool is_read, uint32_t num_views_limit, uint32_t thread_id, uint64_t &kernel_red_count,
                    uint64_t &kernel_count, std::vector<TopPair> &top_pairs) {
  using std::string;
  using std::to_string;
  using std::make_tuple;
  using std::get;
  using std::endl;
  string r = is_read ? "read" : "write";
  std::ofstream out("temporal_" + r + "_top" + to_string(num_views_limit) + "_" + to_string(thread_id) + ".csv",
                    std::ios::app);
  out << "kernel id," << kernel_id << endl;
  out << "redundant access num,all access num,redundancy rate" << endl;
  out << kernel_red_count << "," << kernel_count << "," << (double) kernel_red_count / kernel_count
      << endl;
  if (not top_pairs.empty()) {
    out << "f_cubin_id,f_function_index,f_pc_offset,t_cubin_id,t_function_index,t_pc_offest,value,kind,num_units,count"
        << endl;
    for (auto &apair: top_pairs) {
      // <pc_from, pc_to, value, Accesskind, count>
      out << apair.from_pc.cubin_id << "," << apair.from_pc.function_index << "," << apair.from_pc.pc << ","
          << apair.to_pc.cubin_id << "," << apair.to_pc.function_index << "," << apair.to_pc.pc << ",";
      output_kind_value(apair.value, apair.kind, out.rdbuf(), true);
      out << "," << apair.kind.to_string() << ",x" << apair.kind.vec_size / apair.kind.unit_size << ","
          << apair.count << endl;
    }
    out.close();
  }
}


void get_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessKind access_kind,
                       SpatialTrace &spatial_trace) {
  spatial_trace[std::make_pair(memory_op_id, access_kind)][pc][value] += 1;
}


void record_spatial_trace(SpatialTrace &spatial_trace,
                          redshow_record_data_t &record_data, uint32_t num_views_limit,
                          SpatialStatistic &spatial_statistic, SpatialStatistic &thread_spatial_statistic) {
  // Pick top record data views
  TopViews top_views;
  // memory_iter: {<memory_op_id, AccessKind> : {pc: {value: counter}}}
  for (auto &memory_iter : spatial_trace) {
    // pc_iter: {pc: {value: counter}}
    for (auto &pc_iter : memory_iter.second) {
      auto pc = pc_iter.first;
      // vale_iter: {value: counter}
      for (auto &val_iter : pc_iter.second) {
        auto count = val_iter.second;
        spatial_statistic[memory_iter.first][val_iter.first] = count;
        thread_spatial_statistic[memory_iter.first][val_iter.first] = count;
        redshow_record_view_t view;
        view.pc_offset = pc;
        view.memory_id = 0;
        view.count = count;
        if (top_views.size() < num_views_limit) {
          top_views.push(view);
        } else {
          auto &top = top_views.top();
          if (top.count < view.count) {
            top_views.pop();
            top_views.push(view);
          }
        }
      }
    }
  }

  auto num_views = 0;
  // Put top record data views into record_data
  while (top_views.empty() == false) {
    auto &view = top_views.top();
    record_data.views[num_views++] = view;
    top_views.pop();
  }
  record_data.num_views = num_views;
}


void
show_spatial_trace(uint32_t thread_id, uint64_t kernel_id, SpatialStatistic &spatial_statistic,
                   uint32_t num_write_limit, bool is_read, bool is_kernel) {
  using std::endl;
  using std::to_string;
  using std::get;
  std::string r = is_read ? "read" : "write";
  std::ofstream out("spatial_" + r + "_top" + to_string(num_write_limit) + "_" + to_string(thread_id) + ".csv",
                    std::ios::app);
  if (!is_kernel) {
    out << "===========,summary for the a cpu thread,===========" << endl;
  } else {
    out << "kernel id," << kernel_id << ",";
  }
  out << "size,";
  out << spatial_statistic.size() << endl;
  // {<memory_op_id, AccessKind>: {value: count}}
  for (auto &memory_iter: spatial_statistic) {
    out << "memory_op_id," << memory_iter.first.first << ",";
    // [(value, count, AccessKind)]
    TopStatistic top_statistic;
    u64 all_count = 0;
    // value_iter:  {value: count}
    for (auto &value_iter: memory_iter.second) {
      all_count += value_iter.second;
      if (top_statistic.size() < num_write_limit) {
        top_statistic.push(std::make_tuple(get<0>(value_iter), get<1>(value_iter), get<1>(memory_iter.first)));
      } else {
        auto &top = top_statistic.top();
        if (value_iter.second > get<1>(top)) {
          top_statistic.pop();
          top_statistic.push(std::make_tuple(get<0>(value_iter), get<1>(value_iter), get<1>(memory_iter.first)));
        }
      }
    }
    out << "sum_count," << all_count << endl;
    out << "value,count,rate,kind,num_units" << endl;
    // write to file
    while (not top_statistic.empty()) {
      auto top = top_statistic.top();
      top_statistic.pop();
      auto value = get<0>(top);
      auto count = get<1>(top);
      auto akind = get<2>(top);
      output_kind_value(value, akind, out.rdbuf(), true);
      // out<<std::hex<<get<0>(top)<<std::dec;
      out << "," << count << "," << (double) count / all_count << "," << akind.to_string() << ",x"
          << akind.vec_size / akind.unit_size << endl;
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
  }
  return a;
}


void output_kind_value(u64 a, AccessKind akind, std::streambuf *buf, bool is_signed) {
  std::ostream out(buf);
  if (akind.data_type == REDSHOW_DATA_INT) {
    if (akind.unit_size == 8) {
      if (is_signed) {
        int8_t b6;
        memcpy(&b6, &a, sizeof(b6));
        out << (int) b6;
      } else {
        u8 b7;
        memcpy(&b7, &a, sizeof(b7));
        out << b7;
      }
    } else if (akind.unit_size == 16) {
      if (is_signed) {
        short int b8;
        memcpy(&b8, &a, sizeof(b8));
        out << b8;
      } else {
        unsigned short int b9;
        memcpy(&b9, &a, sizeof(b9));
        out << b9;
      }
    } else if (akind.unit_size == 32) {
      if (is_signed) {
        int b4;
        memcpy(&b4, &a, sizeof(b4));
        out << b4;
      } else {
        uint32_t b5;
        memcpy(&b5, &a, sizeof(b5));
        out << b5;
      }
    } else if (akind.unit_size == 64) {
      if (is_signed) {
        long long b3;
        memcpy(&b3, &a, sizeof(b3));
        out << b3;
      } else {
        out << a;
      }
    }
  } else if (akind.data_type == REDSHOW_DATA_FLOAT) {
    // At this time, it must be float
    if (akind.unit_size == 32) {
      float b1;
      memcpy(&b1, &a, sizeof(b1));
      out << b1;
    } else if (akind.unit_size == 64) {
      double b2;
      memcpy(&b2, &a, sizeof(b2));
      out << b2;
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
  u32 c = a & 0xffffffffu;
  u64 bits = 23 - decimal_degree_f32;
  u64 mask = 0xffffffffffffffff << bits;
  c &= mask;
  u64 b = 0;
  memcpy(&b, &c, sizeof(c));
  return b;
}
