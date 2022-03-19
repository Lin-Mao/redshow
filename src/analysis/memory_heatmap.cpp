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
    
    _memories.try_emplace(op->op_id, op);

    // TODO(@Mao): to store the _memories which is logging the allocated memory. Think about how to update it.

  } else { // is_submemory == true
    
    _sub_memories.try_emplace(op->op_id, op);
  }
 
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


void MemoryHeatmap::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {

}


void MemoryHeatmap::analysis_end(u32 cpu_thread, i32 kernel_id) {

}


void MemoryHeatmap::block_enter(const ThreadId &thread_id) {

}


void MemoryHeatmap::block_exit(const ThreadId &thread_id) {

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
  // printf("op_id:%lu, start:%lu, end:%lu, len:%lu\n", memory.op_id, memory_range.start, memory_range.end, memory.len);

  auto heatmap = _heatmap_list.find(memory.op_id);
  if (heatmap == _heatmap_list.end()) {
    auto object = _memories.at(memory.op_id);
    size_t len = object->len / access_kind.unit_size;
    
    int* arr = (int*) malloc(sizeof(int) * len);
    memset(arr, 0, sizeof(int) * len);

    HeatMapMemory heatmap(len);
    heatmap.array = arr;
    _heatmap_list[memory.op_id] = heatmap;
  }
  
  update_heatmap_list(memory.op_id, memory_range, access_kind.unit_size);

}

  // Flush
void MemoryHeatmap::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {

}


void MemoryHeatmap::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {
  
  std::ofstream out(output_dir + "memory_heatmap" + ".csv");
  
  for (auto iter : _heatmap_list) {
    out << "mem_id " << iter.first << "(" << _op_node.at(iter.first) << ")" << std::endl;
    for (size_t i = 0; i < iter.second.size; i++) {
      out << int(iter.second.array[i]) << " ";
      if (i % 100 == 99) out << std::endl;
    }
    out << std::endl;
    out << std::endl;
  }


  out.close();

}

}   // namespace redshow 