//
// Created by find on 19-7-1.
//

#include "common_lib.h"
#include "redshow.h"


void get_temporal_trace(u64 pc, ThreadId tid, u64 addr, u64 value, AccessType::DataType access_type,
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


void get_spatial_trace(u64 pc, u64 value, u64 memory_op_id, AccessType::DataType access_type,
  SpatialTrace &spatial_trace) {
  spatial_trace[std::make_tuple(memory_op_id, access_type)][pc][value] += 1;
}


void record_spatial_trace(SpatialTrace &spatial_trace,
  redshow_record_data_t &record_data, uint32_t num_views_limit) {
  // Pick top record data views
  TopViews top_views;
  // memory_iter: {<memory_op_id, AccessType::DataType> : {pc: {value: counter}}}
  for (auto &memory_iter : spatial_trace) {
    // pc_iter: {pc: {value: counter}}
    for (auto &pc_iter : memory_iter.second) {
      auto pc = pc_iter.first;
      // vale_iter: {value: counter}
      for (auto &val_iter : pc_iter.second) {
        auto count = val_iter.second;
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


void show_spatial_trace() {
}
