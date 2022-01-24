/**
 * @file memory_heatmap.cpp
 * @author @Lin-Mao
 * @brief New mode in GVPorf for memory profiling
 * @version 0.1
 * @date 2022-01-18
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "analysis/memory_heatmap.h"
#include <cstring>


// #define DEBUG_PRINT

// #define SUB_MEMORY

#ifdef DEBUG_PRINT
#include <iostream>
#endif

namespace redshow {

void MemoryHeatmap::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}


void MemoryHeatmap::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory /* default = false */) {
  update_op_node(op->op_id, op->ctx_id);
  if (!is_submemory) {

#ifdef DEBUG_PRINT
    std::cout << std::endl;
    std::cout << "<DEBUG>---------------------memory_op_callback---------------------" << std::endl;
    std::cout << "op_id:" << op->op_id << " range[" << op->memory_range.start << ", " 
    << op->memory_range.end << "]" << " size(" << op->memory_range.end - op->memory_range.start 
    << " B)" << std::endl;
    std::cout << "---------------------memory_op_callback---------------------<DEBUG>" << std::endl;
    std::cout << std::endl;
#endif
    
    _memories.try_emplace(op->op_id, op);

    // TODO(@Mao): to store the _memories which is logging the allocated memory. Think about how to update it.

  } else { // is_submemory == true
    
    _sub_memories.try_emplace(op->op_id, op);
  }
 
}


void MemoryHeatmap::kernel_op_callback(std::shared_ptr<Kernel> op) {

}


void MemoryHeatmap::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
// TODO(@Mao):
  lock();
  
  if (op->type == OPERATION_TYPE_KERNEL) {
    // kernel_op_callback(std::dynamic_pointer_cast<Kernel>(op));
  } else if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op), is_submemory);
  } else if (op->type == OPERATION_TYPE_MEMFREE) {
    // memfree_op_callback(std::dynamic_pointer_cast<Memfree>(op), is_submemory);
  }

  unlock();
}


void MemoryHeatmap::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {
    // Do not need to know value and need to get interval of memory
    assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

    lock();

    if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<MemoryHeatmapTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
    }

    _trace = std::dynamic_pointer_cast<MemoryHeatmapTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

    unlock();
}


void MemoryHeatmap::analysis_end(u32 cpu_thread, i32 kernel_id) {

}


void MemoryHeatmap::block_enter(const ThreadId &thread_id) {

}


void MemoryHeatmap::block_exit(const ThreadId &thread_id) {

}


void MemoryHeatmap::merge_memory_range(Set<MemoryRange> &memory, const MemoryRange &memory_range) {
  auto start = memory_range.start;
  auto end = memory_range.end;

  auto liter = memory.prev(memory_range);
  if (liter != memory.end()) { // @Mao: xxx.end is not the last iterator but the next of the last iterator and has no meaning
    if (liter->end >= memory_range.start) {
      if (liter->end < memory_range.end) {
        // overlap and not covered
        start = liter->start;
        liter = memory.erase(liter);
      } else {
        // Fully covered
        return;
      }
    } else {
      liter++;
    }
  }

  bool range_delete = false;
  auto riter = liter;
  if (riter != memory.end()) {
    if (riter->start <= memory_range.end) {
      if (riter->end < memory_range.end) {
        // overlap and not covered
        range_delete = true;
        riter = memory.erase(riter);
      } else if (riter->start == memory_range.start) {
        // riter->end >= memory_range.end
        // Fully covered
        return;
      } else {
        // riter->end >= memory_range.end
        // Partial covered
        end = riter->end;
        riter = memory.erase(riter);
      }
    }
  }

  while (range_delete) {
    range_delete = false;
    if (riter != memory.end()) {
      if (riter->start <= memory_range.end) {
        if (riter->end < memory_range.end) {
          // overlap and not covered
          range_delete = true;
        } else {
          // Partial covered
          end = riter->end;
        }
        riter = memory.erase(riter);
      }
    }
  }

  if (riter != memory.end()) {
    // Hint for constant time insert: emplace(h, p) if p is before h
    memory.emplace_hint(riter, start, end);
  } else {
    memory.emplace(start, end);
  }
}

void MemoryHeatmap::update_heatmap_list(u64 op_id, MemoryRange memory_range, uint32_t unit_size) {
  auto &heatmap = _heatmap_list[op_id];

  auto memory = _memories.at(op_id);
  auto start = (memory_range.start - memory->memory_range.start) / unit_size;
  auto end = (memory_range.end - memory->memory_range.start) / unit_size;

  for (int i = start; i < end; i++) {
    *(heatmap.array + i) += 1;
    
  }
  
}


// not a whole buffer, but a part buffer in a memory object
void MemoryHeatmap::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {

if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  auto &memory_range = memory.memory_range;

  auto heatmap = _heatmap_list.find(memory.op_id);
  if (heatmap == _heatmap_list.end()) {
    auto object = _memories.at(memory.op_id);
    size_t len = object->len / access_kind.unit_size;
    
    uint8_t* arr = (uint8_t*) malloc(sizeof(uint8_t) * len);
    memset(arr, 0, sizeof(uint8_t) * len);

    HeatMapMemory heatmap(len);
    heatmap.array = arr;
    _heatmap_list[memory.op_id] = heatmap;
  }
  
  update_heatmap_list(memory.op_id, memory_range, access_kind.unit_size);

  // if (flags & GPU_PATCH_READ) {
  //   // add for heatmap
  //   update_heatmap_list(memory.op_id, memory_range, access_kind.unit_size);
  //   // if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
  //   //   merge_memory_range(_trace->read_memory[memory.op_id], memory_range);
  //   // } else if (_trace->read_memory[memory.op_id].empty()) {
  //   //   _trace->read_memory[memory.op_id].insert(memory_range);
  //   // }
  // }
  // if (flags & GPU_PATCH_WRITE) {
  //   // add for heatmap
  //   update_heatmap_list(memory.op_id, memory_range, access_kind.unit_size);
  //   // merge_memory_range(_trace->write_memory[memory.op_id], memory_range);
  // }

}

  // Flush
void MemoryHeatmap::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {
  
  // std::ofstream out(output_dir + "memory_profile_thread_flush" + std::to_string(cpu_thread) + ".csv");

  // out << "This is flush thread test." << std::endl;
  // out.close();

}


void MemoryHeatmap::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {
  
  std::ofstream out(output_dir + "memory_heatmap" + ".csv");
  
  for (auto iter : _heatmap_list) {
    out << "mem_id " << _op_node.at(iter.first) << std::endl;
    for (size_t i = 0; i < iter.second.size; i++) {
      out << int(iter.second.array[i]) << " ";
      if (i % 30 == 29) out << std::endl;
    }
    out << std::endl;
    out << std::endl;
  }


  out.close();

}

}   // namespace redshow 