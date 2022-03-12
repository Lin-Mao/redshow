/**
 * @file memory_liveness.cpp
 * @author @Lin-Mao
 * @brief Split the liveness part from memory profile mode. Faster to get the liveness.
 * @version 0.1
 * @date 2022-03-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "analysis/memory_liveness.h"

namespace redshow {

void MemoryLiveness::op_callback(OperationPtr op, bool is_submemory) {
  lock();
  
  if (op->type == OPERATION_TYPE_KERNEL) {
    kernel_op_callback(std::dynamic_pointer_cast<Kernel>(op));
  } else if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op), is_submemory);
  } else if (op->type == OPERATION_TYPE_MEMFREE) {
    memfree_op_callback(std::dynamic_pointer_cast<Memfree>(op), is_submemory);
  } else if (op->type == OPERATION_TYPE_MEMCPY) {
    memcpy_op_callback(std::dynamic_pointer_cast<Memcpy>(op));
  } else if (op->type == OPERATION_TYPE_MEMSET) {
    memset_op_callback(std::dynamic_pointer_cast<Memset>(op));
  }

  unlock();
}

void MemoryLiveness::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}

void MemoryLiveness::memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op) {
  auto op_list = _operations.find(memory_op_id);

  if (op_list == _operations.end()) {
    Map<u64, memory_operation> op_list_temp;
    op_list_temp.emplace(op_id, mem_op);
    _operations.emplace(memory_op_id, op_list_temp);
  } else {
    auto operation = op_list->second.find(op_id);
    if (operation == op_list->second.end()) {
      op_list->second.emplace(op_id, mem_op);
    }
  }
}

void MemoryLiveness::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {
  update_op_node(op->op_id, op->ctx_id);
  if (!is_submemory) {
    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);

    if (!_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, ALLOC);
    }
  }
  

}

void MemoryLiveness::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {
  update_op_node(op->op_id, op->ctx_id);
  if (!is_submemory) {
    u64 address = op->memory_range.start;
    u64 malloc_op_id = _addresses_map.at(address);
    
    _addresses_map.erase(address);
    _current_memories.erase(malloc_op_id);

    memory_operation_register(malloc_op_id, op->op_id, FREE);

  }

}

void MemoryLiveness::kernel_op_callback(std::shared_ptr<Kernel> op) {
  update_op_node(op->op_id, op->ctx_id);

  if (_trace.get() == NULL) {
    // If the kernel is sampled
    return;
  }

  for (auto &trace_iter : _trace->access_memory) {
    memory_operation_register(trace_iter.first, _trace->kernel.op_id, ACCESS);
  }


  // reset trace
  _trace->access_memory.clear();
  _trace = NULL;

}

void MemoryLiveness::memcpy_op_callback(std::shared_ptr<Memcpy> op) {
  update_op_node(op->op_id, op->ctx_id);

  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {
    memory_operation_register(op->src_memory_op_id, op->op_id, COPYF);
  }

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    memory_operation_register(op->dst_memory_op_id, op->op_id, COPYT);
  }


}

void MemoryLiveness::memset_op_callback(std::shared_ptr<Memset> op) {
  update_op_node(op->op_id, op->ctx_id);

  memory_operation_register(op->memory_op_id, op->op_id, SET);

}


void MemoryLiveness::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type) {
  // Do not need to know value and need to get interval of memory
  assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

  lock();

  if (!this->_kernel_trace[cpu_thread].has(host_op_id)) {
  auto trace = std::make_shared<MemoryLivenessTrace>();
  trace->kernel.ctx_id = kernel_id;
  trace->kernel.cubin_id = cubin_id;
  trace->kernel.mod_id = mod_id;
  trace->kernel.op_id = host_op_id;
  this->_kernel_trace[cpu_thread][host_op_id] = trace;
  }

  // ?? How to make sure this _trace are the same with the _trace in kernel_op_callback
  _trace = std::dynamic_pointer_cast<MemoryLivenessTrace>(this->_kernel_trace[cpu_thread][host_op_id]);

  unlock();
}

void MemoryLiveness::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void MemoryLiveness::block_enter(const ThreadId &thread_id) {}

void MemoryLiveness::block_exit(const ThreadId &thread_id) {}

void MemoryLiveness::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {
  if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  if (!_trace->access_memory.has(memory.op_id)) {
    _trace->access_memory.emplace(memory.op_id, true);
  }
  

}

void MemoryLiveness::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void MemoryLiveness::output_memory_operation_list(std::string file_name) {
  std::ofstream out(file_name);

  for (auto ops : _operations) {
    out << "[" << ops.first << "]: ";
    for (auto iter : ops.second) {
      out << iter.first << "(" << iter.second << ")" << ", ";
    }
    out << std::endl;
  }

}

void MemoryLiveness::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {
  Vector<int> vec;

  output_memory_operation_list(output_dir + "memory_liveness.txt");

}

}   // namespace redshow