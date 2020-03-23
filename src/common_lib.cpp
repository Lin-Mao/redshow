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
                           uint32_t num_views_limit, uint64_t &kernel_red_count, uint64_t &kernel_count) {
  // Pick top record data views
  TopViews top_views;
  // {pc1 : {pc2 : {<value, type>}}}
  for (auto &from_pc_iter : pc_pairs) {
    for (auto &to_pc_iter : from_pc_iter.second) {
      auto to_pc = to_pc_iter.first;
      // {<value, type> : count}
      for (auto &val_iter : to_pc_iter.second) {
        auto val = val_iter.first.first;
        auto count = val_iter.second;
        redshow_record_view_t view;
        view.pc_offset = to_pc;
        view.memory_id = 0;
        view.count = count;
        kernel_red_count += count;
        view.access_sum_count = pc_access_sum[to_pc];
        kernel_count += view.access_sum_count;
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
show_temporal_trace(u64 kernel_id, PCPairs &pc_pairs, PCAccessSum &pc_access_sum, bool is_read,
                    uint32_t num_views_limit, uint32_t thread_id, uint64_t &kernel_red_count,
                    uint64_t &kernel_count) {
  using std::string;
  using std::to_string;
  using std::map;
  using std::pair;
  using std::tuple;
  using std::make_tuple;
  using std::get;
  using std::endl;
  string r = is_read ? "read" : "write";
  std::ofstream out("temporal_" + r + "_top" + to_string(num_views_limit) + "_" + to_string(thread_id) + ".csv",
                    std::ios::app);
  out << "kernel id," << kernel_id << endl;
  // <pc_from, pc_to, value, Accesstype, count>
  TopPairs top_pairs;
  for (auto &from_pc_iter: pc_pairs) {
    for (auto &to_pc_iter: from_pc_iter.second) {
      // {<value, AccessKind> : count}}
      for (auto &value_iter: to_pc_iter.second) {
        if (top_pairs.size() < num_views_limit) {
          top_pairs.push(
              make_tuple(from_pc_iter.first, to_pc_iter.first, value_iter.first.first, value_iter.first.second,
                         value_iter.second));
        } else {
          auto &top = top_pairs.top();
          if (get<4>(top) < value_iter.second) {
            top_pairs.pop();
            top_pairs.push(
                make_tuple(from_pc_iter.first, to_pc_iter.first, value_iter.first.first, value_iter.first.second,
                           value_iter.second));
          }
        }
      }
    }
  }
  out << "redundant access num, all access num, redundancy rate" << endl;
  out << kernel_red_count << "," << kernel_count << "," << (double) kernel_red_count / kernel_count
      << endl;
  if (not top_pairs.empty()) {
    out << "from_pc,to_pc,value,type,count" << endl;
    while (not top_pairs.empty()) {
      // <pc_from, pc_to, value, Accesstype, count>
      auto top = top_pairs.top();
      top_pairs.pop();
      auto pc_from = get<0>(top);
      auto pc_to = get<1>(top);
      auto value = get<2>(top);
      auto akind = get<3>(top);
      auto count = get<4>(top);
      out << pc_from << "," << pc_to << ",";
      output_corresponding_type_value(value, akind, out.rdbuf(), true);
      out << "," << combine_type_unitsize(akind) << "," << count << endl;
    }
    out << endl;
  }
  out.close();

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
    out << "===========↓↓↓↓summary for the a cpu thread↓↓↓↓===========" << endl;
  } else {
    out << "=======================" << endl;
    out << "kernel id," << kernel_id << ",";
  }
  out << "size,";
  out << spatial_statistic.size() << endl;
  // {<memory_op_id, AccessKind>: {value: count}}
  for (auto &memory_iter: spatial_statistic) {
    out << "memory_op_id," << memory_iter.first.first << ",";
    // [(value, count, Accesstype)]
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
    out << "value,count,rate,type" << endl;
    // write to file
    while (not top_statistic.empty()) {
      auto top = top_statistic.top();
      top_statistic.pop();
      auto value = get<0>(top);
      auto count = get<1>(top);
      auto akind = get<2>(top);
      output_corresponding_type_value(value, akind, out.rdbuf(), true);
      out << "," << count << "," << (double) count / all_count << "," << combine_type_unitsize(akind) << endl;
    }
    out << endl;
  }
  if (!is_kernel) {
    out << "===========↑↑↑↑summary for the a cpu thread↑↑↑↑===========" << endl;
  }
  out.close();
}


u64 store2basictype(u64 a, AccessKind akind, int decimal_degree_f32, int decimal_degree_f64) {
  switch (akind.data_type) {
    case AccessKind::UNKNOWN:
      break;
    case AccessKind::INTEGER:
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
    case AccessKind::FLOAT:
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


void output_corresponding_type_value(u64 a, AccessKind akind, std::streambuf *buf, bool is_signed) {
  std::ostream out(buf);
  if (akind.data_type == AccessKind::INTEGER) {
    if (akind.unit_size == 8) {
      if (is_signed) {
        char b6;
        memcpy(&b6, &a, sizeof(b6));
        out << b6;
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
  } else if (akind.data_type == AccessKind::FLOAT) {
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


std::string combine_type_unitsize(AccessKind akind) {
  using std::to_string;
  switch (akind.data_type) {
    case AccessKind::UNKNOWN:
      break;
    case AccessKind::INTEGER:
      return "int" + to_string(akind.unit_size);
    case AccessKind::FLOAT:
      return "float" + to_string(akind.unit_size);
  }
  return "null";
}
