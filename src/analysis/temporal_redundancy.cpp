#include "analysis/temporal_redundancy.h"

#include <cstring>

#include "operation/kernel.h"
#include "redshow.h"

namespace redshow {

void TemporalRedundancy::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
  // Nothing
}

void TemporalRedundancy::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_DEFAULT);

  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<RedundancyTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace =
      std::dynamic_pointer_cast<RedundancyTrace>(this->_kernel_trace.at(cpu_thread).at(kernel_id));

  unlock();
}

void TemporalRedundancy::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace = NULL; }

void TemporalRedundancy::block_enter(const ThreadId &thread_id) {
  // nothing
}

void TemporalRedundancy::block_exit(const ThreadId &thread_id) {
  _trace->read_temporal_trace.erase(thread_id);
  _trace->write_temporal_trace.erase(thread_id);
}

void TemporalRedundancy::unit_access(i32 kernel_id, const ThreadId &thread_id,
                                     const AccessKind &access_kind, const Memory &memory, u64 pc,
                                     u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  addr += index * access_kind.unit_size / 8;

  if (flags & GPU_PATCH_READ) {
    auto &pc_pairs = _trace->read_pc_pairs;
    auto &temporal_trace = _trace->read_temporal_trace;
    update_temporal_trace(pc, thread_id, addr, value, access_kind, temporal_trace, pc_pairs);
    _trace->read_pc_count[pc]++;
  }
  
  if (flags & GPU_PATCH_WRITE) {
    auto &pc_pairs = _trace->write_pc_pairs;
    auto &temporal_trace = _trace->write_temporal_trace;
    update_temporal_trace(pc, thread_id, addr, value, access_kind, temporal_trace, pc_pairs);
    _trace->write_pc_count[pc]++;
  }
}

void TemporalRedundancy::flush_thread(u32 cpu_thread, const std::string &output_dir,
                                      const LockableMap<u32, Cubin> &cubins,
                                      redshow_record_data_callback_func record_data_callback) {
  u32 pc_views_limit = 0;
  u32 mem_views_limit = 0;

  redshow_pc_views_get(&pc_views_limit);
  redshow_mem_views_get(&mem_views_limit);

  redshow_record_data_t record_data;

  record_data.views = new redshow_record_view_t[pc_views_limit]();

  lock();

  auto &thread_kernel_trace = this->_kernel_trace.at(cpu_thread);

  unlock();

  std::ofstream out_read(output_dir + "temporal_read_t" + std::to_string(cpu_thread) + ".csv");
  std::ofstream out_write(output_dir + "temporal_write_t" + std::to_string(cpu_thread) + ".csv");

  u64 thread_count = 0;
  u64 thread_read_temporal_count = 0;
  u64 thread_write_temporal_count = 0;
  for (auto &trace_iter : thread_kernel_trace) {
    auto kernel_id = trace_iter.first;
    auto trace = std::dynamic_pointer_cast<RedundancyTrace>(trace_iter.second);
    auto &kernel = trace->kernel;
    auto cubin_id = kernel.cubin_id;
    auto mod_id = kernel.mod_id;
    auto cubin_offset = 0;
    u64 kernel_read_temporal_count = 0;
    u64 kernel_write_temporal_count = 0;
    u64 kernel_count = 0;
    TemporalStatistics read_temporal_stats;
    TemporalStatistics write_temporal_stats;
    cubins.lock();
    auto &symbols = cubins.at(cubin_id).symbols.at(mod_id);
    cubins.unlock();

    record_data.analysis_type = REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY;
    // Read
    record_data.access_type = REDSHOW_ACCESS_READ;
    record_temporal_trace(pc_views_limit, mem_views_limit, trace->read_pc_pairs,
                          trace->read_pc_count, read_temporal_stats, record_data,
                          kernel_read_temporal_count);

    symbols.transform_data_views(record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_temporal_statistics(cubin_id, symbols, read_temporal_stats);

    // Write
    record_data.access_type = REDSHOW_ACCESS_WRITE;
    record_temporal_trace(pc_views_limit, mem_views_limit, trace->write_pc_pairs,
                          trace->write_pc_count, write_temporal_stats, record_data,
                          kernel_write_temporal_count);

    symbols.transform_data_views(record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_temporal_statistics(cubin_id, symbols, write_temporal_stats);

    // Accumulate all access count and red count
    for (auto &iter : trace->read_pc_count) {
      kernel_count += iter.second;
    }

    for (auto &iter : trace->write_pc_count) {
      kernel_count += iter.second;
    }

    thread_count += kernel_count;
    thread_read_temporal_count += kernel_read_temporal_count;
    thread_write_temporal_count += kernel_write_temporal_count;

    if (mem_views_limit != 0) {
      if (!read_temporal_stats.empty()) {
        show_temporal_trace(cpu_thread, kernel_id, kernel_read_temporal_count, kernel_count,
                            read_temporal_stats, false, out_read);
      }
      if (!write_temporal_stats.empty()) {
        show_temporal_trace(cpu_thread, kernel_id, kernel_write_temporal_count, kernel_count,
                            write_temporal_stats, false, out_write);
      }
    }
  }

  if (mem_views_limit != 0) {
    if (thread_count != 0) {
      TemporalStatistics read_temporal_stats;
      TemporalStatistics write_temporal_stats;

      show_temporal_trace(cpu_thread, 0, thread_read_temporal_count, thread_count,
                          read_temporal_stats, true, out_read);
      show_temporal_trace(cpu_thread, 0, thread_write_temporal_count, thread_count,
                          write_temporal_stats, true, out_write);
    }
  }

  out_read.close();
  out_write.close();

  // Release data
  delete[] record_data.views;
}

void TemporalRedundancy::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                               redshow_record_data_callback_func record_data_callback) {}

void TemporalRedundancy::update_temporal_trace(u64 pc, ThreadId thread_id, u64 addr, u64 value,
                                               AccessKind access_kind,
                                               TemporalTrace &temporal_trace, PCPairs &pc_pairs) {
  auto tmr_it = temporal_trace.find(thread_id);
  // Record current operation.
  Map<u64, std::pair<u64, u64>> record;
  record[addr] = std::make_pair(pc, value);
  if (tmr_it == temporal_trace.end()) {
    // The trace doesn't have the thread's record
    temporal_trace[thread_id] = record;
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

void TemporalRedundancy::record_temporal_trace(u32 pc_views_limit, u32 mem_views_limit,
                                               PCPairs &pc_pairs, PCAccessCount &pc_access_count,
                                               TemporalStatistics &temporal_stats,
                                               redshow_record_data_t &record_data,
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

void TemporalRedundancy::show_temporal_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count,
                                             u64 total_count, TemporalStatistics &temporal_stats,
                                             bool is_thread, std::ofstream &out) {
  using std::endl;
  using std::get;
  using std::make_tuple;
  using std::string;
  using std::to_string;
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
}

void TemporalRedundancy::transform_temporal_statistics(uint32_t cubin_id,
                                                       const SymbolVector &symbols,
                                                       TemporalStatistics &temporal_stats) {
  for (auto &temp_stat_iter : temporal_stats) {
    for (auto &real_pc_pair : temp_stat_iter.second) {
      auto &to_real_pc = real_pc_pair.to_pc;
      auto &from_real_pc = real_pc_pair.from_pc;
      // to_real_pc
      auto ret = symbols.transform_pc(to_real_pc.pc_offset);
      if (ret.has_value()) {
        to_real_pc.cubin_id = ret.value().cubin_id;
        to_real_pc.function_index = ret.value().function_index;
        to_real_pc.pc_offset = ret.value().pc_offset;
      }
      // from_real_pc
      ret = symbols.transform_pc(from_real_pc.pc_offset);
      if (ret.has_value()) {
        from_real_pc.cubin_id = ret.value().cubin_id;
        from_real_pc.function_index = ret.value().function_index;
        from_real_pc.pc_offset = ret.value().pc_offset;
      }
    }
  }
}

}  // namespace redshow
