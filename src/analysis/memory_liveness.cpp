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

void MemoryLiveness::update_ctx_table(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    auto ctx_table_node = _ctx_table.find(ctx_id);
    if (ctx_table_node == _ctx_table.end()) {
      Vector<u64> op_vec;
      op_vec.push_back(op_id);
      _ctx_table.emplace(ctx_id, op_vec);
    } else {
      ctx_table_node->second.push_back(op_id);
    }
  }
}

void MemoryLiveness::update_ctx_node(i32 ctx_id, memory_operation op) {
  if (!_ctx_node.has(ctx_id)) {
    _ctx_node.emplace(ctx_id, op);
  }
}

void MemoryLiveness::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}

void MemoryLiveness::memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op) {
  auto ops_node = _operations.find(memory_op_id);

  if (ops_node == _operations.end()) {  // for malloc
    Map<u64, memory_operation> op_list;
    op_list.emplace(op_id, mem_op);
    _operations.emplace(memory_op_id, op_list);
  } else {
    auto operation = ops_node->second.find(op_id);
    if (operation == ops_node->second.end()) {
      ops_node->second.emplace(op_id, mem_op);
    }
  }
}

void MemoryLiveness::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {
  update_op_node(op->op_id, op->ctx_id);
  update_ctx_table(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, ALLOC);

  if (!is_submemory) {
    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _memory_size_list.push_back(MemoryEntry(op->op_id, op->len));

    if (!_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, ALLOC);
    }
  }
  

}

void MemoryLiveness::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {
  update_op_node(op->op_id, op->ctx_id);
  update_ctx_table(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, FREE);

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
  update_ctx_table(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, ACCESS);
  _kernel_op_node[op->op_id] = op->ctx_id;

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
  // update_op_node(op->op_id, op->ctx_id);

  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {
    update_ctx_node(op->ctx_id, COPYF);
    memory_operation_register(op->src_memory_op_id, op->op_id, COPYF);
  }

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    update_ctx_node(op->ctx_id, COPYT);
    memory_operation_register(op->dst_memory_op_id, op->op_id, COPYT);
  }


}

void MemoryLiveness::memset_op_callback(std::shared_ptr<Memset> op) {
  // update_op_node(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, SET);

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

  out.close();
}

void MemoryLiveness::output_memory_size_list(std::string file_name) {
  
  // TODO(@Lin-Mao): can be changed to quick sort
  // sort the vector
  for (int i = 0; i < _memory_size_list.size()-1; i++) {
    int index = i;
    MemoryEntry temp = _memory_size_list[i];

    for (int j = i + 1; j < _memory_size_list.size(); j++) {
      if (_memory_size_list[j] > temp) {
        index = j;
        temp = _memory_size_list[j];
      }
    }

    if (index != i) {
      _memory_size_list[index] = _memory_size_list[i];
      _memory_size_list[i] = temp;
    }
  }

  std::ofstream output (file_name);

  for (auto iter : _memory_size_list)
    output << "op_id=" << iter.op_id << ", size=" << iter.size << std::endl;

  output.close();

}

void MemoryLiveness::output_kernel_list(std::string file_name) {
  
  std::ofstream output(file_name);

  for (auto op_ctx : _kernel_op_node) {
    output << op_ctx.first << ", " << op_ctx.second << std::endl;
  }
  
  output.close();

}

void MemoryLiveness::output_ctx_node(std::string file_name) {

  std::ofstream output(file_name);

  for (auto ctx : _ctx_node) {
    if (ctx.second == ALLOC) {
      output << "ALLOC " << ctx.first << std::endl; 
    } else if (ctx.second == FREE) {
      output << "FREE " << ctx.first << std::endl;
    } else if (ctx.second == ACCESS) {
      output << "ACCESS " << ctx.first << std::endl;
    } else if (ctx.second == SET) {
      output << "SET " << ctx.first << std::endl;
    } else if (ctx.second == COPYT) {
      output << "COPYT " << ctx.first << std::endl;
    } else if (ctx.second == COPYF) {
      output << "COPYF " << ctx.first << std::endl;
    }
  }
  output.close();
}

void MemoryLiveness::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {

  output_memory_operation_list(output_dir + "memory_liveness.txt");

  output_memory_size_list(output_dir + "memory_size_list.txt");

  output_kernel_list(output_dir + "kernel_list.txt");

  output_ctx_node(output_dir + "memory_liveness.csv");

}

}   // namespace redshow