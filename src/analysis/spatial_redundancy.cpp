#include "analysis/spatial_redundancy.h"

#include <cstring>

#include "common/vector.h"
#include "operation/kernel.h"
#include "redshow.h"

namespace redshow {

void SpatialRedundancy::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
  // Nothing
}

void SpatialRedundancy::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_DEFAULT);

  lock();

  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<RedundancyTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
  }

  _trace = std::dynamic_pointer_cast<RedundancyTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

  unlock();
}

void SpatialRedundancy::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void SpatialRedundancy::block_enter(const ThreadId &thread_id) {
  // nothing
}

void SpatialRedundancy::block_exit(const ThreadId &thread_id) {
  // nothing
}

void SpatialRedundancy::unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id,
                                    const AccessKind &access_kind, const Memory &memory, u64 pc,
                                    u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  addr += index * access_kind.unit_size / 8;
  
  if (flags & GPU_PATCH_READ) {
    auto &spatial_trace = _trace->read_spatial_trace;
    update_spatial_trace(pc, value, memory.op_id, access_kind, spatial_trace);
    _trace->read_pc_count[pc]++;
  }
  
  if (flags & GPU_PATCH_WRITE) {
    auto &spatial_trace = _trace->write_spatial_trace;
    update_spatial_trace(pc, value, memory.op_id, access_kind, spatial_trace);
    _trace->write_pc_count[pc]++;
  }
}

void SpatialRedundancy::update_spatial_trace(u64 pc, u64 value, u64 memory_op_id,
                                             AccessKind access_kind, SpatialTrace &spatial_trace) {
  spatial_trace[std::make_pair(memory_op_id, access_kind)][pc][value] += 1;
}

void SpatialRedundancy::flush_thread(u32 cpu_thread, const std::string &output_dir,
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

  std::ofstream out_read(output_dir + "spatial_read_t" + std::to_string(cpu_thread) + ".csv");
  std::ofstream out_write(output_dir + "spatial_write_t" + std::to_string(cpu_thread) + ".csv");

  u64 thread_count = 0;
  u64 thread_read_spatial_count = 0;
  u64 thread_write_spatial_count = 0;
  for (auto &trace_iter : thread_kernel_trace) {
    auto kernel_id = trace_iter.first;
    auto trace = std::dynamic_pointer_cast<RedundancyTrace>(trace_iter.second);
    auto &kernel = trace->kernel;
    auto cubin_id = kernel.cubin_id;
    auto mod_id = kernel.mod_id;
    auto cubin_offset = 0;
    u64 kernel_read_spatial_count = 0;
    u64 kernel_write_spatial_count = 0;
    u64 kernel_count = 0;
    SpatialStatistics read_spatial_stats;
    SpatialStatistics write_spatial_stats;
    cubins.lock();
    auto &symbols = cubins.at(cubin_id).symbols.at(mod_id);
    cubins.unlock();

    record_data.analysis_type = REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY;
    // read
    record_data.access_type = REDSHOW_ACCESS_READ;
    record_spatial_trace(pc_views_limit, mem_views_limit, trace->read_spatial_trace,
                         trace->read_pc_count, read_spatial_stats, record_data,
                         kernel_read_spatial_count);
    // Transform pcs
    symbols.transform_data_views(record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_spatial_statistics(cubin_id, symbols, read_spatial_stats);

    // Write
    record_data.access_type = REDSHOW_ACCESS_WRITE;
    record_spatial_trace(pc_views_limit, mem_views_limit, trace->write_spatial_trace,
                         trace->write_pc_count, write_spatial_stats, record_data,
                         kernel_write_spatial_count);

    // Transform pcs
    symbols.transform_data_views(record_data);
    record_data_callback(cubin_id, kernel_id, &record_data);
    transform_spatial_statistics(cubin_id, symbols, write_spatial_stats);

    // Accumulate all access count and red count
    for (auto &iter : trace->read_pc_count) {
      kernel_count += iter.second;
    }

    for (auto &iter : trace->write_pc_count) {
      kernel_count += iter.second;
    }

    thread_count += kernel_count;
    thread_read_spatial_count += kernel_read_spatial_count;
    thread_write_spatial_count += kernel_write_spatial_count;

    if (mem_views_limit != 0) {
      if (!read_spatial_stats.empty()) {
        show_spatial_trace(cpu_thread, kernel_id, kernel_read_spatial_count, kernel_count,
                           read_spatial_stats, false, out_read);
      }
      if (!write_spatial_stats.empty()) {
        show_spatial_trace(cpu_thread, kernel_id, kernel_write_spatial_count, kernel_count,
                           write_spatial_stats, false, out_write);
      }
    }
  }

  if (mem_views_limit != 0) {
    if (thread_count != 0) {
      SpatialStatistics read_spatial_stats;
      SpatialStatistics write_spatial_stats;

      show_spatial_trace(cpu_thread, 0, thread_read_spatial_count, thread_count, read_spatial_stats,
                         true, out_read);
      show_spatial_trace(cpu_thread, 0, thread_write_spatial_count, thread_count,
                         write_spatial_stats, true, out_write);
    }
  }

  out_read.close();
  out_write.close();

  // Release data
  delete[] record_data.views;
}

void SpatialRedundancy::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                              redshow_record_data_callback_func record_data_callback) {}

void SpatialRedundancy::record_spatial_trace(u32 pc_views_limit, u32 mem_views_limit,
                                             SpatialTrace &spatial_trace,
                                             PCAccessCount &pc_access_count,
                                             SpatialStatistics &spatial_stats,
                                             redshow_record_data_t &record_data,
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

void SpatialRedundancy::show_spatial_trace(u32 cpu_thread, i32 kernel_id, u64 total_red_count,
                                           u64 total_count, SpatialStatistics &spatial_stats,
                                           bool is_thread, std::ofstream &out) {
  using std::endl;
  using std::get;
  using std::to_string;
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
}

void SpatialRedundancy::transform_spatial_statistics(u32 cubin_id, const SymbolVector &symbols,
                                                     SpatialStatistics &spatial_stats) {
  for (auto &spatial_stat_iter : spatial_stats) {
    for (auto &pc_iter : spatial_stat_iter.second) {
      for (auto &real_pc_pair : pc_iter.second) {
        auto &to_real_pc = real_pc_pair.to_pc;
        // to_real_pc
        auto ret = symbols.transform_pc(to_real_pc.pc_offset);
        if (ret.has_value()) {
          to_real_pc.cubin_id = ret.value().cubin_id;
          to_real_pc.function_index = ret.value().function_index;
          to_real_pc.pc_offset = ret.value().pc_offset;
        }
      }
    }
  }
}

}  // namespace redshow
