//
// Created by find on 19-7-1.
//

#include "analysis/redundancy.h"

#include <cstring>

#include "redshow.h"

namespace redshow {

void Redundancy::analysis_begin(u32 cpu_thread, i32 kernel_id) {
  _trace = &(_kernel_trace.at(cpu_thread).at(kernel_id));
}

void Redundancy::analysis_end(u32 cpu_thread, i32 kernel_id) {
  _trace = NULL;
}

void Redundancy::block_enter(const ThreadId &tid) {
  // nothing
}

void Redundancy::block_exit(const ThreadId &tid) {
  _trace->read_temporal_trace.erase(tid);
  _trace->write_temporal_trace.erase(tid);
}

void Redundancy::unit_access(const ThreadId &tid, const AccessKind &access_kind, u64 memory_op_id,
                             u64 pc, u64 value, u64 addr, u32 stride, u32 index, bool read) {
  addr += index * stride;
  if (read) {
    auto &pc_pairs = _trace->read_pc_pairs;
    auto &temporal_trace = _trace->read_temporal_trace;
    update_temporal_trace(pc, tid, addr, value, memory.op_id, access_kind, temporal_trace,
                          pc_pairs);

    auto &spatial_trace = _trace->read_spatial_trace;
    update_spatial_trace(pc, value, memory.op_id, access_kind, spatial_trace);
  } else {
    auto &pc_pairs = _trace->write_pc_pairs;
    auto &temporal_trace = _trace->write_temporal_trace;
    update_temporal_trace(pc, tid, addr, value, memory.op_id, access_kind, temporal_trace,
                          pc_pairs);

    auto &spatial_trace = _trace->write_spatial_trace;
    update_spatial_trace(pc, value, memory.op_id, access_kind, spatial_trace);
  }
}

void Redundancy::flush_thread(u32 cpu_thread, const std::string &output_dir,
                              const Map<u32, Cubin> &cubins,
                              redshow_record_data_callback_func *record_data_callback) {
  u32 pc_views_limit = 0; 
  u32 mem_views_limit = 0; 

  redshow_pc_views_get(&pc_views_limit);
  redshow_mem_views_get(&mem_views_limit);

  redshow_record_data_t record_data;

  record_data.views = new redshow_record_view_t[pc_views_limit]();

  _kernel_trace.lock();
  auto &thread_kernel_trace = _kernel_trace.at(cpu_thread);
  _kernel_trace.unlock();

  u64 thread_count = 0;
  u64 thread_read_temporal_count = 0;
  u64 thread_write_temporal_count = 0;
  u64 thread_read_spatial_count = 0;
  u64 thread_write_spatial_count = 0;
  for (auto &kernel_iter : thread_kernel_trace) {
    auto kernel_id = kernel_iter.first;
    auto &kernel = kernel_iter.second;
    auto cubin_id = kernel.cubin_id;
    auto mod_id = kernel.mod_id;
    auto cubin_offset = 0;
    u64 kernel_read_temporal_count = 0;
    u64 kernel_write_temporal_count = 0;
    u64 kernel_read_spatial_count = 0;
    u64 kernel_write_spatial_count = 0;
    u64 kernel_count = 0;
    SpatialStatistics read_spatial_stats;
    SpatialStatistics write_spatial_stats;
    TemporalStatistics read_temporal_stats;
    TemporalStatistics write_temporal_stats;
    auto &symbols = cubin_map[cubin_id].symbols[mod_id];

    record_data.analysis_type = REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY;
    // read
    record_data.access_type = REDSHOW_ACCESS_READ;
    record_spatial_trace(kernel.read_spatial_trace, kernel.read_pc_count, pc_views_limit,
                         mem_views_limit, record_data, read_spatial_stats,
                         kernel_read_spatial_count);
    // Transform pcs
    transform_data_views(symbols, record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_spatial_statistics(cubin_id, symbols, read_spatial_stats);

    // Write
    record_data.access_type = REDSHOW_ACCESS_WRITE;
    record_spatial_trace(kernel.write_spatial_trace, kernel.write_pc_count, pc_views_limit,
                         mem_views_limit, record_data, write_spatial_stats,
                         kernel_write_spatial_count);
    // Transform pcs
    transform_data_views(symbols, record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_spatial_statistics(cubin_id, symbols, write_spatial_stats);

    record_data.analysis_type = REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY;
    // Read
    record_data.access_type = REDSHOW_ACCESS_READ;
    record_temporal_trace(kernel.read_pc_pairs, kernel.read_pc_count, pc_views_limit,
                          mem_views_limit, record_data, read_temporal_stats,
                          kernel_read_temporal_count);

    transform_data_views(symbols, record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_temporal_statistics(cubin_id, symbols, read_temporal_stats);

    // Write
    record_data.access_type = REDSHOW_ACCESS_WRITE;
    record_temporal_trace(kernel.write_pc_pairs, kernel.write_pc_count, pc_views_limit,
                          mem_views_limit, record_data, write_temporal_stats,
                          kernel_write_temporal_count);

    transform_data_views(symbols, record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_temporal_statistics(cubin_id, symbols, write_temporal_stats);

    // Accumulate all access count and red count
    for (auto &iter : kernel.read_pc_count) {
      kernel_count += iter.second;
    }

    for (auto &iter : kernel.write_pc_count) {
      kernel_count += iter.second;
    }

    thread_count += kernel_count;
    thread_read_temporal_count += kernel_read_temporal_count;
    thread_write_temporal_count += kernel_write_temporal_count;
    thread_read_spatial_count += kernel_read_spatial_count;
    thread_write_spatial_count += kernel_write_spatial_count;

    if (mem_views_limit != 0) {
      if (!read_temporal_stats.empty()) {
        redshow::show_temporal_trace(thread_id, kernel_id, kernel_read_temporal_count, kernel_count,
                                     read_temporal_stats, true, false);
      }
      if (!write_temporal_stats.empty()) {
        redshow::show_temporal_trace(thread_id, kernel_id, kernel_write_temporal_count,
                                     kernel_count, write_temporal_stats, false, false);
      }
    }

    if (mem_views_limit != 0) {
      if (!read_spatial_stats.empty()) {
        redshow::show_spatial_trace(thread_id, kernel_id, kernel_read_spatial_count, kernel_count,
                                    read_spatial_stats, true, false);
      }
      if (!write_spatial_stats.empty()) {
        redshow::show_spatial_trace(thread_id, kernel_id, kernel_write_spatial_count, kernel_count,
                                    write_spatial_stats, false, false);
      }
    }
  }

  if (mem_views_limit != 0) {
    if (thread_count != 0) {
      SpatialStatistics read_spatial_stats;
      SpatialStatistics write_spatial_stats;
      TemporalStatistics read_temporal_stats;
      TemporalStatistics write_temporal_stats;

      show_temporal_trace(cpu_thread, 0, thread_read_temporal_count, thread_count,
                          read_temporal_stats, true, true);
      show_temporal_trace(cpu_thread, 0, thread_write_temporal_count, thread_count,
                          write_temporal_stats, false, true);
      show_spatial_trace(cpu_thread, 0, thread_read_spatial_count, thread_count,
                         read_spatial_stats, true, true);
      show_spatial_trace(cpu_thread, 0, thread_write_spatial_count, thread_count,
                                  write_spatial_stats, false, true);
    }
  }

  // Release data
  delete[] record_data.views;
}

void Redundancy::flush(const Map<u32, Cubin> &cubins, const std::string &output_dir,
                       const Vector<OperationPtr> operations,
                       redshow_record_data_callback_func *record_data_callback) {}

void Redundancy::update_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value,
                                       AccessKind access_kind, TemporalTrace &temporal_trace,
                                       PCPairs &pc_pairs) {
  auto tmr_it = temporal_trace.find(tid);
  // Record current operation.
  Map<u64, std::pair<u64, u64>> record;
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

void record_temporal_trace(i32 kernel_id, PCPairs &pc_pairs, PCAccessCount &pc_access_count, u32 pc_views_limit,
                           u32 mem_views_limit, redshow_record_data_t &record_data,
                           u64 &kernel_temporal_count) {
  auto &temporal_stats = _kernel_trace.at(kernel_id);
          
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

void Redundancy::show_temporal_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count, u64 total_count,
                         bool is_read, bool is_thread) {
  using std::endl;
  using std::get;
  using std::make_tuple;
  using std::string;
  using std::to_string;
  auto &temporal_stats = _kernel_trace.at(kernel_id);
  string r = is_read ? "read" : "write";
  std::ofstream out("temporal_" + r + "_t" + to_string(cpu_thread) + ".csv", std::ios::app);
  if (is_thread) {
    out << "cpu_thread," << cpu_thread << endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double)total_red_count / total_count
        << endl;
  } else {
    out << "kernel_id," << kernel_id << endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double)total_red_count / total_count
        << endl;
    out << "cubin_id,f_function_index,f_pc_offset,t_function_index,t_pc_offest,value,data_type,"
           "vector_size,unit_size,count,rate,norm_rate"
        << endl;
    for (auto temp_iter : temporal_stats) {
      for (auto &real_pc_pair : temp_iter.second) {
        auto to_real_pc = real_pc_pair.to_pc;
        auto from_real_pc = real_pc_pair.from_pc;
        out << from_real_pc.cubin_id << "," << from_real_pc.function_index << ","
            << from_real_pc.pc_offset << "," << to_real_pc.function_index << ","
            << to_real_pc.pc_offset << ",";
        out << real_pc_pair.access_kind.value_to_string(real_pc_pair.value, true);
        out << "," << real_pc_pair.access_kind.to_string() << "," << real_pc_pair.red_count << ","
            << static_cast<double>(real_pc_pair.red_count) / real_pc_pair.access_count << ","
            << static_cast<double>(real_pc_pair.red_count) / total_count << endl;
      }
    }
  }
  out.close();
}

void Redundancy::update_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessKind access_kind,
                                      SpatialTrace &spatial_trace) {
  spatial_trace[std::make_pair(memory_op_id, access_kind)][pc][value] += 1;
}

void Redundancy::record_spatial_trace(i32 kernel_id,
                          u32 pc_views_limit, u32 mem_views_limit,
                          redshow_record_data_t &record_data, 
                          u64 &kernel_spatial_count) {
  auto &spatial_trace = _kernel_trace.at(kernel_id);
  auto &pc_access_count = _kernel_trace.at(kernel_id);
  auto &spatial_stats = _kernel_trace.at(kernel_id);

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

void Redundancy::show_spatial_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count,
                                    u64 total_count, bool is_read, bool is_thread) {
  auto &spatial_stats = _kernel_trace.at(kernel_id);

  using std::endl;
  using std::get;
  using std::to_string;
  std::string r = is_read ? "read" : "write";
  std::ofstream out("spatial_" + r + "_t" + to_string(cpu_thread) + ".csv", std::ios::app);
  if (is_thread) {
    out << "cpu_thread," << kernel_id << std::endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double)total_red_count / total_count
        << endl;
  } else {
    out << "kernel_id," << kernel_id << std::endl;
    out << "redundant_access_count,total_access_count,redundancy_rate" << endl;
    out << total_red_count << "," << total_count << "," << (double)total_red_count / total_count
        << endl;
    out << "memory_op_id,cubin_id,function_index,pc_offset,value,data_type,vector_size,unit_size,"
           "count,rate,norm_rate"
        << endl;
    // {memory_op_id : {pc : [RealPCPair]}}
    for (auto &spatial_iter : spatial_stats) {
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
          out << memory_op_id << "," << cubin_id << "," << function_index << "," << pc_offset
              << ",";
          out << akind.value_to_string(value, true);
          out << "," << akind.to_string() << "," << red_count << ","
              << static_cast<double>(red_count) / access_count << ","
              << static_cast<double>(red_count) / total_count << std::endl;
        }
      }
    }
  }
  out.close();
}

void Redundancy::transform_temporal_statistics(i32 kernel_id, uint32_t cubin_id,
                                               Vector<redshow::Symbol> &symbols) {
  auto &temporal_stats = _kernel_trace.at(kernel_id);
  for (auto &temp_stat_iter : temporal_stats) {
    for (auto &real_pc_pair : temp_stat_iter.second) {
      auto &to_real_pc = real_pc_pair.to_pc;
      auto &from_real_pc = real_pc_pair.from_pc;
      uint32_t function_index = 0;
      uint64_t cubin_offset = 0;
      uint64_t pc_offset = 0;
      // to_real_pc
      transform_pc(symbols, to_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
      to_real_pc.cubin_id = cubin_id;
      to_real_pc.function_index = function_index;
      to_real_pc.pc_offset = pc_offset;
      // from_real_pc
      transform_pc(symbols, from_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
      from_real_pc.cubin_id = cubin_id;
      from_real_pc.function_index = function_index;
      from_real_pc.pc_offset = pc_offset;
    }
  }
}

void Redundancy::transform_spatial_statistics(i32 kernel_id, u32 cubin_id,
                                                     Vector<redshow::Symbol> &symbols) {
  auto &spatial_stats = _kernel_trace.at(kernel_id);
  for (auto &spatial_stat_iter : spatial_stats) {
    for (auto &pc_iter : spatial_stat_iter.second) {
      for (auto &real_pc_pair : pc_iter.second) {
        auto &to_real_pc = real_pc_pair.to_pc;
        uint32_t function_index = 0;
        uint64_t cubin_offset = 0;
        uint64_t pc_offset = 0;
        // to_real_pc
        transform_pc(symbols, to_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
        to_real_pc.cubin_id = cubin_id;
        to_real_pc.function_index = function_index;
        to_real_pc.pc_offset = pc_offset;
      }
    }
  }
}

void Redundancy::transform_data_views(const SymbolVector &symbols, redshow_record_data_t &record_data) {
  // Transform pcs
  for (auto i = 0; i < record_data.num_views; ++i) {
    uint64_t pc = record_data.views[i].pc_offset;
    auto ret = symbols.transform_pc(pc);
    if (ret.has_value()) {
      record_data.views[i].function_index = ret.value()[0];
      record_data.views[i].pc_offset = ret.value()[1];
    }
  }
}

}  // namespace redshow
