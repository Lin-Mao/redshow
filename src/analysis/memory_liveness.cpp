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

#include "../../../torch-monitor/include/torch_monitor.h"

namespace redshow {

const static size_t MAX_NUM_STATES = 30;

thread_local static torch_monitor_python_state_t python_states[MAX_NUM_STATES];

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

void MemoryLiveness::update_torch_python_states(u64 op_id) {
  size_t num_states = 0;
  torch_monitor_python_state_get(MAX_NUM_STATES, python_states, &num_states);

  Vector<PythonState> pstates;
  for (size_t i = 0; i < num_states; ++i) {
    pstates.push_back(PythonState(std::string(python_states[i].file_name), std::string(python_states[i].function_name), \ 
                                  python_states[i].function_first_lineno, python_states[i].lineno));
  }
  _torch_python_states.try_emplace(op_id, pstates);

}

void MemoryLiveness::memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op, bool is_sub) {
  
  if (!is_sub) {
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
  } else {
    auto sub_ops_node = _sub_operations.find(memory_op_id);
    if (sub_ops_node == _sub_operations.end()) {  // for sub-alloc
      Map<u64, memory_operation> sub_op_list;
      sub_op_list.emplace(op_id, mem_op);
      _sub_operations.emplace(memory_op_id, sub_op_list);
    } else {
      auto sub_operation = sub_ops_node->second.find(op_id);
      if (sub_operation == sub_ops_node->second.end()) {
        sub_ops_node->second.emplace(op_id, mem_op);
      }
    }

  }
}

bool MemoryLiveness::verify_sub_allocation_ongpu(std::shared_ptr<Memory> sub_alloc) {
  auto address_entry = _addresses_map.prev(sub_alloc->memory_range.start);
  if (address_entry == _addresses_map.end())
    return false;
  
  auto memory = _current_memories.at(address_entry->second);

  if (sub_alloc->memory_range.start >= memory->memory_range.start \ 
      && sub_alloc->memory_range.end <= memory->memory_range.end) {
        return true;
  } else {
    return false;
  }  
}

void MemoryLiveness::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {

  if (!is_submemory) {
    update_op_node(op->op_id, op->ctx_id);
    update_ctx_table(op->op_id, op->ctx_id);
    update_ctx_node(op->ctx_id, ALLOC);

    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _memory_size_list.push_back(MemoryEntry(op->op_id, op->len));
    _current_memory_usage += op->len;
    _memory_size_log.try_emplace(op->op_id, memory_size("alloc", _current_memory_usage));

    // printf("op_id:%lu, len:%d\n", op->op_id, op->len);
    if (_current_memory_usage > _current_memory_peak) {
      _current_memory_peak = _current_memory_usage;
    }

    if (!_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, ALLOC);
    }
  } else {
    update_torch_python_states(op->op_id);
    _sub_op_node.try_emplace(op->op_id, "ALLOC");

    _submemories.try_emplace(op->op_id, op);
    _current_submemories.try_emplace(op->op_id, op);
    _sub_addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _submemory_size_list.push_back(MemoryEntry(op->op_id, op->len));
    _current_submemory_usage += op->len;

    if (_current_submemory_usage > _current_submemory_peak) {
      _current_submemory_peak = _current_submemory_usage;
    }

    if (!_sub_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, SUBALLOC, true);
    }

  }
  
}

void MemoryLiveness::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {

  if (!is_submemory) {
    update_op_node(op->op_id, op->ctx_id);
    update_ctx_table(op->op_id, op->ctx_id);
    update_ctx_node(op->ctx_id, FREE);

    u64 address = op->memory_range.start;
    u64 malloc_op_id = _addresses_map.at(address);

    _addresses_map.erase(address);
    _current_memories.erase(malloc_op_id);
    _current_memory_usage -= op->len;
    _memory_size_log.emplace(op->op_id, memory_size("free", _current_memory_usage));

    memory_operation_register(malloc_op_id, op->op_id, FREE);

  } else {
    update_torch_python_states(op->op_id);
    _sub_op_node.try_emplace(op->op_id, "FREE");

    u64 address = op->memory_range.start;
    u64 sub_alloc_id = _sub_addresses_map.at(address);

    _sub_addresses_map.erase(address);
    _current_submemories.erase(sub_alloc_id);
    _current_submemory_usage -= op->len;
    
    memory_operation_register(sub_alloc_id, op->op_id, SUBFREE, true);
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

  u64 kernel_memory_usage = 0;
  for (auto &trace_iter : _trace->access_memory) {
    auto memory = _memories.at(trace_iter.first);
    kernel_memory_usage += memory->len;
    memory_operation_register(trace_iter.first, op->op_id, ACCESS);
  }
  if (kernel_memory_usage > _optimal_memory_peak) {
    _memory_peak_kernel = op->op_id;
    _optimal_memory_peak = kernel_memory_usage;
  }

  if (_troch_sub_analysis) {
    u64 kernel_submemory_usage = 0;
    for (auto trace_iter : _trace->access_submemory) {
      auto submemory = _submemories.at(trace_iter.first);
      kernel_submemory_usage += submemory->len;
      memory_operation_register(trace_iter.first, op->op_id, ACCESS, true);
    }
    if (kernel_submemory_usage > _optimal_submemory_peak) {
      _submemory_peak_kernel = op->op_id;
      _optimal_submemory_peak = kernel_submemory_usage;
    }
  }

  // reset trace
  _trace->access_memory.clear();
  _trace->access_submemory.clear();
  _trace = NULL;

}

void MemoryLiveness::memcpy_op_callback(std::shared_ptr<Memcpy> op) {

  printf("=============================================\n");
  printf("op->dst_memory_op_id: %lu\n", op->dst_memory_op_id);
  printf("op->dst_start: %lu\n", op->dst_start);
  printf("op->src_memory_op_id: %lu\n", op->src_memory_op_id);
  printf("op->src_start: %lu\n", op->src_start);
  printf("---------------------------------------------\n");

  if (_troch_sub_analysis) {
    if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {    
      auto liter = _sub_addresses_map.prev(op->src_start);
      auto riter = _sub_addresses_map.prev(op->src_start + op->len);
      if (liter != _sub_addresses_map.end()) 
      {
        if (riter->second != liter->second) {
        for (auto iter = liter; iter != riter; iter++) {
          memory_operation_register(iter->second, op->op_id, COPYF, true);
        }
        memory_operation_register(riter->second, op->op_id, COPYF, true);
        }
      }
    }

    if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
      auto liter = _sub_addresses_map.prev(op->dst_start);
      auto riter = _sub_addresses_map.prev(op->dst_start + op->len);
      if (liter != _sub_addresses_map.end()) {
        if (riter->second != liter->second) {
          for (auto iter = liter; iter != riter; iter++) {
            memory_operation_register(iter->second, op->op_id, COPYT, true);
          }
        }
        memory_operation_register(riter->second, op->op_id, COPYT, true);
      }
    }
  }

  update_op_node(op->op_id, op->ctx_id);
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
  

  if (_troch_sub_analysis) {
    auto liter = _sub_addresses_map.prev(op->start);
    auto riter = _sub_addresses_map.prev(op->start + op->len);
    if (liter->second != riter->second) {
      for (auto iter = liter; iter != riter; iter++) {
        memory_operation_register(iter->second, op->op_id, SET, true);
      }
    }
    memory_operation_register(riter->second, op->op_id, SET, true);
  }

  update_op_node(op->op_id, op->ctx_id);
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

void MemoryLiveness::unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {
  if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  if (!_trace->access_memory.has(memory.op_id)) {
    _trace->access_memory.emplace(memory.op_id, true);
  }

  if (_troch_sub_analysis) {
    auto iter = _sub_addresses_map.prev(memory.memory_range.start); 

    // memory_operation_register(riter->second, host_op_id, ACCESS, true);
    if (!_trace->access_submemory.has(iter->second)) {
      _trace->access_submemory.emplace(iter->second, true);
    }

  }
  

}

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

  output << "optimal_memory_peak: " << _optimal_memory_peak << " B" << std::endl;
  output << "current_memory_peak: " << _current_memory_peak - 512 << " B" << std::endl;
  output << std::endl;

  for (auto op : _op_node) {
    output << "op_id: " << op.first << ", ";
    auto ctx_type =_ctx_node.at(op.second);

    if (ctx_type == ALLOC) {
      output << "ALLOC " << op.second << std::endl; 
    } else if (ctx_type == FREE) {
      output << "FREE " << op.second << std::endl;
    } else if (ctx_type == ACCESS) {
      output << "ACCESS " << op.second << std::endl;
    } else if (ctx_type == SET) {
      output << "SET " << op.second << std::endl;
    } else if (ctx_type == COPYT) {
      output << "COPYT " << op.second << std::endl;
    } else if (ctx_type == COPYF) {
      output << "COPYF " << op.second << std::endl;
    } else if (ctx_type == SUBALLOC) {
      output << "SUBALLOC" << op.second << std::endl;
    } else if (ctx_type == SUBFREE) {
      output << "SUBFREE" << op.second << std::endl;
    }

  }

  output.close();
}


void MemoryLiveness::output_op_sequence(std::string filename) {
  std::ofstream output(filename);
  output << "memory_peak_kernel: " << _memory_peak_kernel << std::endl;
  output << "optimal_memory_peak: " << _optimal_memory_peak << " B" << std::endl;
  output << "current_memory_peak: " << _current_memory_peak - 512 << " B" << std::endl << std::endl;

  for (auto op : _memory_size_log) {
    output << op.first << "(" << op.second.op << "): " << op.second.size << " B" << std::endl;
  }
  output.close();
}

void MemoryLiveness::output_submemory_liveness(std::string file_name) {
  std::ofstream output(file_name);
  for (auto sub_ops : _sub_operations) {
    output << "[" << sub_ops.first << "]: ";
    for (auto iter : sub_ops.second) {
      output << iter.first << "(" << iter.second << ")" << ", ";
    }
    output << std::endl;
  }
  output.close();
}

void MemoryLiveness::output_submemory_size_list(std::string file_name) {

  // sort the vector
  for (int i = 0; i < _submemory_size_list.size()-1; i++) {
    int index = i;
    MemoryEntry temp = _submemory_size_list[i];

    for (int j = i + 1; j < _submemory_size_list.size(); j++) {
      if (_submemory_size_list[j] > temp) {
        index = j;
        temp = _submemory_size_list[j];
      }
    }
    if (index != i) {
      _submemory_size_list[index] = _submemory_size_list[i];
      _submemory_size_list[i] = temp;
    }
  }

  std::ofstream output(file_name);
  
  for (auto iter : _submemory_size_list) {
    output << "op_id=" << iter.op_id << ", size=" << iter.size << std::endl;
  }
  output.close();
}

void MemoryLiveness::output_submemory_info(std::string file_name) {
  std::ofstream output(file_name);
  output << "submemory_peak_kernel: " << _submemory_peak_kernel << std::endl;
  output << "optimal_submemory_peak: " << _optimal_submemory_peak << " B" << std::endl;
  output << "current_submemory_peak: " << _current_submemory_peak - 512 << " B" << std::endl << std::endl;
  output.close();
}

void MemoryLiveness::output_torch_python_states(std::string filename) {
  std::ofstream output(filename);
  for (auto miter : _torch_python_states) {
    output << "------------------------------" << std::endl;
    output << _sub_op_node.at(miter.first) << ": " << miter.first << std::endl;
    int count = 0;
    for (auto viter : miter.second) {
      output << "(" << count << ")"
             << "File: " <<  viter.file_name << std::endl;
      output << "\tFunction: " << viter.function_name << std::endl;
      output << "\tFirst line: " << viter.function_first_lineno << std::endl;
      output << "\tCall at line: " << viter.lineno << std::endl;
      count++;
    }

    output << std::endl;
  }
}

void MemoryLiveness::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void MemoryLiveness::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {

   printf("ssssssssssssssssssssssssssssssssssssss\n");

  output_memory_operation_list(output_dir + "memory_liveness.txt");

  printf("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");

  output_memory_size_list(output_dir + "memory_size_list.txt");

  printf("ddddddddddddddddddddddddddddddddddddd\n");

  output_kernel_list(output_dir + "kernel_list.txt");

  printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");

  output_ctx_node(output_dir + "memory_liveness.csv");

  printf("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\n");

  output_op_sequence(output_dir + "memory_size_op.txt");

  printf("ssssssssssssssssssssssssssssssssssssss\n");

  output_submemory_liveness(output_dir + "submemory_liveness.txt");

  printf("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");

  output_submemory_size_list(output_dir + "submemory_size_list.txt");

  printf("ddddddddddddddddddddddddddddddddddddd\n");

  output_submemory_info(output_dir + "submemory_info.txt");

  printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");

  output_torch_python_states(output_dir + "torch_python_states.txt");

  printf("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\n");

}

}   // namespace redshow