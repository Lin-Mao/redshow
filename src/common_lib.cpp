//
// Created by find on 19-7-1.
//

#include "common_lib.h"
#include "redshow.h"


void get_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value, AccessType access_type,
                        TemporalTrace &temporal_trace, PCPairs &pc_pairs) {
  auto tmr_it = temporal_trace.find(tid);
  // Record current operation.
  std::map<u64, std::tuple<u64, u64>> record;
  record[addr] = std::make_tuple(pc, value);
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
      auto prev_pc = std::get<0>(m_it->second);
      auto prev_value = std::get<1>(m_it->second);
      if (prev_value == value) {
        pc_pairs[prev_pc][pc][std::make_tuple(prev_value, access_type)] += 1;
      }
      m_it->second = record[addr];
    }
  }
}


void record_temporal_trace(PCPairs &pc_pairs,
                           redshow_record_data_t &record_data, uint32_t num_views_limit) {
  // Pick top record data views
  TopViews top_views;
  // {pc1 : {pc2 : {<value, type>}}}
  for (auto &from_pc_iter : pc_pairs) {
    for (auto &to_pc_iter : from_pc_iter.second) {
      auto to_pc = to_pc_iter.first;
      // {<value, type> : count}
      for (auto &val_iter : to_pc_iter.second) {
        auto val = std::get<0>(val_iter.first);
        auto count = val_iter.second;
        redshow_record_view_t view;
        view.pc_offset = to_pc;
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


void show_temporal_trace() {
}


void get_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessType access_type,
                       SpatialTrace &spatial_trace) {
  spatial_trace[std::make_tuple(memory_op_id, access_type)][pc][value] += 1;
}


void record_spatial_trace(SpatialTrace &spatial_trace,
                          redshow_record_data_t &record_data, uint32_t num_views_limit,
                          SpatialStatistic &spatial_statistic) {
  // Pick top record data views
  TopViews top_views;
  // memory_iter: {<memory_op_id, AccessType> : {pc: {value: counter}}}
  for (auto &memory_iter : spatial_trace) {
    // pc_iter: {pc: {value: counter}}
    for (auto &pc_iter : memory_iter.second) {
      auto pc = pc_iter.first;
      // vale_iter: {value: counter}
      for (auto &val_iter : pc_iter.second) {
        auto count = val_iter.second;
        spatial_statistic[memory_iter.first][val_iter.first] = count;
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
show_spatial_trace(uint32_t thread_id, SpatialStatistic &spatial_statistic, uint32_t num_write_limit, bool is_read) {
  using std::endl;
  using std::to_string;
  using std::get;
  std::string r = is_read ? "read" : "write";
  std::ofstream out("spatial_" + r + "_top" + to_string(num_write_limit) + "_" + to_string(thread_id) + ".csv",
                    std::ios::app);
  out << "size;";
  out << spatial_statistic.size() << endl;
  // {<memory_op_id, AccessType>: {value: count}}
  for (auto &memory_iter: spatial_statistic) {
    out << "memory_id," << get<0>(memory_iter.first) << ",";
//  [(value, count, Accesstype)]
    TopStatistic top_statistic;
    u64 all_count = 0;
//  value_iter:  {value: count}
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
    out << "value,count,rate,type,unit_size" << endl;
//    write to file
    while (not top_statistic.empty()) {
      auto top = top_statistic.top();
      top_statistic.pop();

      output_corresponding_type_value(get<0>(top), get<2>(top), out.rdbuf(), true);
//      out<<std::hex<<get<0>(top)<<std::dec;
      out << "," << get<1>(top) << "," << (double) get<1>(top) / all_count << "," << get<2>(top).type << ","
          << get<2>(top).unit_size << endl;
    }
    out << endl;
  }
  out.close();
}


u64 store2basictype(u64 a, AccessType atype, int decimal_degree_f32, int decimal_degree_f64) {
  switch (atype.type) {
    case AccessType::UNKNOWN:
      break;
    case AccessType::INTEGER:
      switch (atype.unit_size) {
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
    case AccessType::FLOAT:
      switch (atype.unit_size) {
        case 32:
          return store2float(a, decimal_degree_f32);
        case 64:
          return store2double(a, decimal_degree_f64);
      }
      break;
  }
  return a;
}


void output_corresponding_type_value(u64 a, AccessType atype, std::streambuf *buf, bool is_signed) {
  std::ostream out(buf);
  if (atype.type == AccessType::INTEGER) {
    if (atype.unit_size == 8) {
      if (is_signed) {
        char b6;
        memcpy(&b6, &a, sizeof(b6));
        out << b6;
      } else {
        u8 b7;
        memcpy(&b7, &a, sizeof(b7));
        out << b7;
      }
    } else if (atype.unit_size == 16) {
      if (is_signed) {
        short int b8;
        memcpy(&b8, &a, sizeof(b8));
        out << b8;
      } else {
        unsigned short int b9;
        memcpy(&b9, &a, sizeof(b9));
        out << b9;
      }
    } else if (atype.unit_size == 32) {
      if (is_signed) {
        int b4;
        memcpy(&b4, &a, sizeof(b4));
        out << b4;
      } else {
        uint32_t b5;
        memcpy(&b5, &a, sizeof(b5));
        out << b5;
      }
    } else if (atype.unit_size == 64) {
      if (is_signed) {
        long long b3;
        memcpy(&b3, &a, sizeof(b3));
        out << b3;
      } else {
        out << a;
      }
    }
//    At this time, it must be float
  } else if (atype.type == AccessType::FLOAT) {
    if (atype.unit_size == 32) {
      float b1;
      memcpy(&b1, &a, sizeof(b1));
      out << b1;
    } else if (atype.unit_size == 64) {
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
