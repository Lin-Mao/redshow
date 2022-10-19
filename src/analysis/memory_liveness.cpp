/**
 * @file memory_liveness.cpp
 * @author @Lin-Mao
 * @brief Memory liveness analysis.
 * @version 0.1
 * @date 2022-03-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "analysis/memory_liveness.h"

#include "torch_monitor.h"

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <cxxabi.h>

namespace redshow {

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS

const static size_t MAX_NUM_STATES = 30;

thread_local static torch_monitor_python_state_t python_states[MAX_NUM_STATES];

void MemoryLiveness::update_torch_python_states(u64 op_id) {
  size_t num_states = 0;
  torch_monitor_python_state_get(MAX_NUM_STATES, python_states, &num_states);

  Vector<PythonState> pstates;
  for (size_t i = 0; i < num_states; ++i) {
    pstates.push_back(
      PythonState(
        std::string(python_states[i].file_name),
        std::string(python_states[i].function_name),
        python_states[i].function_first_lineno,
        python_states[i].lineno)
      );
  }
  _torch_python_states.try_emplace(op_id, pstates);

}

// Call this function to get a backtrace.
void MemoryLiveness::get_torch_libunwind_backtrace(u64 op_id) {

  unw_cursor_t cursor;
  unw_context_t context;

  // Initialize cursor to current frame for local unwinding.
  unw_getcontext(&context);
  unw_init_local(&cursor, &context);

  Vector<Libunwind_Frame> frames;
  // Unwind frames one by one, going up the frame stack.
  while (unw_step(&cursor) > 0) {
    unw_word_t offset, pc;
    unw_get_reg(&cursor, UNW_REG_IP, &pc);
    if (pc == 0) {
      break;
    }
    // std::printf("0x%lx:", pc);

    char sym[256];
    if (unw_get_proc_name(&cursor, sym, sizeof(sym), &offset) == 0) {
      char* nameptr = sym;
      int status;
      char* demangled = abi::__cxa_demangle(sym, nullptr, nullptr, &status);

      if (status == 0) {
        nameptr = demangled;
      }

      // std::printf(" (%s+0x%lx)\n", nameptr, offset);
      frames.push_back(Libunwind_Frame(pc, offset, nameptr));

      std::free(demangled);  
    } else {
      // std::printf(" -- error: unable to obtain symbol name for this frame\n");
      frames.push_back(
        Libunwind_Frame(pc, " -- error: unable to obtain symbol name for this frame")
      );
    }
  }
  _memory_libunwind_frames.try_emplace(op_id, frames);
}

#endif

void MemoryLiveness::update_aux_hit(void* aux, u64 kernel_op_id, bool is_sub) {
  gpu_patch_aux_address_dict_t* kernel_aux = static_cast<gpu_patch_aux_address_dict_t*>(aux);

  if (!is_sub) {
    u64 kernel_memory_usage = 0;
    for (int i = 0; i < kernel_aux->size; i++) {
      if (kernel_aux->hit[i] == 1) {
        u64 memory_op_id = _addresses_map.at(kernel_aux->start_end[i].start);
        memory_operation_register(memory_op_id, kernel_op_id, REDSHOW_MEMORY_ACCESS);
        kernel_memory_usage += _current_memories.at(memory_op_id)->len;
      }
    }

    if (_optimal_memory_peak < kernel_memory_usage) {
      _memory_peak_kernel = kernel_op_id;
      _optimal_memory_peak = kernel_memory_usage;
    }
  } else {
#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS

    u64 kernel_submemory_usage = 0;
    for (int i = 0; i < kernel_aux->size; i++) {
      if (kernel_aux->hit[i] == 1) {
        u64 sub_memory_op_id = _sub_addresses_map.at(kernel_aux->start_end[i].start);
        memory_operation_register(sub_memory_op_id, kernel_op_id, REDSHOW_MEMORY_ACCESS, true);
        kernel_submemory_usage += _current_submemories.at(sub_memory_op_id)->len;
      }
    }

    if (_optimal_submemory_peak < kernel_submemory_usage) {
      _submemory_peak_kernel = kernel_op_id;
      _optimal_submemory_peak = kernel_submemory_usage;
    }

#endif
  }
  

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

void MemoryLiveness::update_ctx_node(i32 ctx_id, memory_operation_t op) {
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

void MemoryLiveness::update_stream_for_ops (u32 stream_id, u64 op_id, memory_operation_t op_type) {
  
  auto opnode = _op_to_stream.find(op_id);
  if (opnode == _op_to_stream.end()) {
    _op_to_stream.emplace(op_id, op_streams(op_type, stream_id));
  } else {
    if (op_type == REDSHOW_MEMORY_COPYT || op_type == REDSHOW_MEMORY_COPYF) {
      opnode->second.op_type2 = op_type;
      opnode->second.stream_id2 = stream_id;
    }
  }

  auto stream = _stream_to_op.find(stream_id);
  if (stream == _stream_to_op.end()) {
    Vector<u64> op_list;
    op_list.push_back(op_id);
    _stream_to_op.emplace(stream_id, op_list);
  } else {
    stream->second.push_back(op_id);
  }

}

void MemoryLiveness::memory_operation_register(
  u64 memory_op_id, u64 op_id, memory_operation_t mem_op, bool is_sub
) {

  if (!is_sub) {
    auto ops_node = _operations.find(memory_op_id);

    if (ops_node == _operations.end()) {  // for malloc
      Map<u64, memory_operation_t> op_list;
      op_list.emplace(op_id, mem_op);
      _operations.emplace(memory_op_id, op_list);
    } else {
      auto operation = ops_node->second.find(op_id);
      if (operation == ops_node->second.end()) {
        ops_node->second.emplace(op_id, mem_op);
      }
    }
  } else {
#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS

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
#endif
  }

}

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

void MemoryLiveness::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {
  update_global_op_id_start(op->op_id);

  if (!is_submemory) {
    update_op_node(op->op_id, op->ctx_id);
    update_stream_for_ops(op->stream_id, op->op_id, REDSHOW_MEMORY_ALLOC);
    // update_ctx_table(op->op_id, op->ctx_id);
    update_ctx_node(op->ctx_id, REDSHOW_MEMORY_ALLOC);
    update_graph_at_malloc(op->op_id, op->stream_id, REDSHOW_MEMORY_ALLOC);
    _read_nodes.emplace(op->op_id, Vector<u64>());

    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _memory_size_list.push_back(MemoryEntry(op->op_id, op->len));
    _current_memory_usage += op->len;
    _memory_size_log.try_emplace(op->op_id, memory_size("alloc", _current_memory_usage));

    // printf("op_id:%lu, len:%d\n", op->op_id, op->len);
    if (_current_memory_usage > _current_memory_peak)
      _current_memory_peak = _current_memory_usage;

    if (!_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, REDSHOW_MEMORY_ALLOC);
    }
  } else {
#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
    get_torch_libunwind_backtrace(op->op_id);

    update_torch_python_states(op->op_id);
    _sub_op_node.try_emplace(op->op_id, "ALLOC");

    _submemories.try_emplace(op->op_id, op);
    _current_submemories.try_emplace(op->op_id, op);
    _sub_addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _submemory_size_list.push_back(MemoryEntry(op->op_id, op->len));
    _current_submemory_usage += op->len;
    _submemory_size_log.try_emplace(op->op_id, memory_size("alloc", _current_submemory_usage));

    if (_current_submemory_usage > _current_submemory_peak) {
      _current_submemory_peak = _current_submemory_usage;
    }

    if (!_sub_operations.has(op->op_id)) {
      memory_operation_register(op->op_id, op->op_id, REDSHOW_SUBMEMORY_ALLOC, true);
    }

#endif
  }

}

void MemoryLiveness::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {
  // update_global_op_id_start(op->op_id);

  if (!is_submemory) {
    update_op_node(op->op_id, op->ctx_id);
    update_stream_for_ops(op->stream_id, op->op_id, REDSHOW_MEMORY_FREE);
    // update_ctx_table(op->op_id, op->ctx_id);
    update_ctx_node(op->ctx_id, REDSHOW_MEMORY_FREE);

    u64 address = op->memory_range.start;
    u64 malloc_op_id = _addresses_map.at(address);
    
    _addresses_map.erase(address);
    _current_memories.erase(malloc_op_id);
    _current_memory_usage -= op->len;
    _memory_size_log.emplace(op->op_id, memory_size("free", _current_memory_usage));

    memory_operation_register(malloc_op_id, op->op_id, REDSHOW_MEMORY_FREE);
    update_graph_at_free(op->op_id, op->stream_id, malloc_op_id, REDSHOW_MEMORY_FREE);

  } else {
#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
    get_torch_libunwind_backtrace(op->op_id);

    update_torch_python_states(op->op_id);
    _sub_op_node.try_emplace(op->op_id, "FREE");

    u64 address = op->memory_range.start;
    u64 sub_alloc_id = _sub_addresses_map.at(address);

    _sub_addresses_map.erase(address);
    _current_submemories.erase(sub_alloc_id);
    _current_submemory_usage -= op->len;
    _submemory_size_log.try_emplace(op->op_id, memory_size("free", _current_submemory_usage));
    
    memory_operation_register(sub_alloc_id, op->op_id, REDSHOW_SUBMEMORY_FREE, true);
#endif
  }

}

void MemoryLiveness::kernel_op_callback(std::shared_ptr<Kernel> op) {
  // update_global_op_id_start(op->op_id);
  update_op_node(op->op_id, op->ctx_id);
  // update_ctx_table(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, REDSHOW_MEMORY_ACCESS);
  update_stream_for_ops(op->stream_id, op->op_id, REDSHOW_MEMORY_ACCESS);
  _kernel_op_node[op->op_id] = op->ctx_id;

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
if (!_submemories.empty()) {
  _sub_op_node.try_emplace(op->op_id, "ACCESS");
  update_torch_python_states(op->op_id);
}
#endif

#ifndef REDSHOW_GPU_ANALYSIS
  if (_trace.get() == NULL) {
    // If the kernel is sampled
    return;
  }

  u64 kernel_memory_usage = 0;
  for (auto &trace_iter : _trace->access_memory) {
    auto memory = _memories.at(trace_iter.first);
    kernel_memory_usage += memory->len;
    memory_operation_register(trace_iter.first, op->op_id, REDSHOW_MEMORY_ACCESS);
  }
  if (kernel_memory_usage > _optimal_memory_peak) {
    _memory_peak_kernel = op->op_id;
    _optimal_memory_peak = kernel_memory_usage;
  }

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
  u64 kernel_submemory_usage = 0;
  for (auto trace_iter : _trace->access_submemory) {
    auto submemory = _submemories.at(trace_iter.first);
    kernel_submemory_usage += submemory->len;
    memory_operation_register(trace_iter.first, op->op_id, REDSHOW_MEMORY_ACCESS, true);
  }
  if (kernel_submemory_usage > _optimal_submemory_peak) {
    _submemory_peak_kernel = op->op_id;
    _optimal_submemory_peak = kernel_submemory_usage;
  }

  _trace->access_submemory.clear();
#endif

  // reset trace
  _trace->access_memory.clear();
  _trace = NULL;
#endif

}

void MemoryLiveness::memcpy_op_callback(std::shared_ptr<Memcpy> op) {
  // update_global_op_id_start(op->op_id);
  update_op_node(op->op_id, op->ctx_id);

  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {
    update_ctx_node(op->ctx_id, REDSHOW_MEMORY_COPYF);
    update_stream_for_ops(op->src_stream_id, op->op_id, REDSHOW_MEMORY_COPYF);
    memory_operation_register(op->src_memory_op_id, op->op_id, REDSHOW_MEMORY_COPYF);
    update_graph_at_access(op->op_id, REDSHOW_MEMORY_COPYF, op->src_stream_id, op->src_memory_op_id, READ);
  }

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    update_ctx_node(op->ctx_id, REDSHOW_MEMORY_COPYT);
    update_stream_for_ops(op->dst_stream_id, op->op_id, REDSHOW_MEMORY_COPYT);
    memory_operation_register(op->dst_memory_op_id, op->op_id, REDSHOW_MEMORY_COPYT);
    update_graph_at_access(op->op_id, REDSHOW_MEMORY_COPYT, op->dst_stream_id, op->dst_memory_op_id, WRITE);
  }

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {    
    auto liter = _sub_addresses_map.prev(op->src_start);
    auto riter = _sub_addresses_map.prev(op->src_start + op->len);
    if (liter != _sub_addresses_map.end()) 
    {
      if (riter->second != liter->second) {
      for (auto iter = liter; iter != riter; iter++) {
        memory_operation_register(iter->second, op->op_id, REDSHOW_MEMORY_COPYF, true);
      }
      memory_operation_register(riter->second, op->op_id, REDSHOW_MEMORY_COPYF, true);
      }
    }
  }

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    auto liter = _sub_addresses_map.prev(op->dst_start);
    auto riter = _sub_addresses_map.prev(op->dst_start + op->len);
    if (liter != _sub_addresses_map.end()) {
      if (riter->second != liter->second) {
        for (auto iter = liter; iter != riter; iter++) {
          memory_operation_register(iter->second, op->op_id, REDSHOW_MEMORY_COPYT, true);
        }
      }
      memory_operation_register(riter->second, op->op_id, REDSHOW_MEMORY_COPYT, true);
    }
  }
#endif


}

void MemoryLiveness::memset_op_callback(std::shared_ptr<Memset> op) {
  // update_global_op_id_start(op->op_id);
  update_op_node(op->op_id, op->ctx_id);
  update_ctx_node(op->ctx_id, REDSHOW_MEMORY_SET);
  update_stream_for_ops(op->stream_id, op->op_id, REDSHOW_MEMORY_SET);
  update_graph_at_access(op->op_id, REDSHOW_MEMORY_SET, op->stream_id, op->memory_op_id, WRITE);

  memory_operation_register(op->memory_op_id, op->op_id, REDSHOW_MEMORY_SET);

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
  auto liter = _sub_addresses_map.prev(op->start);
  auto riter = _sub_addresses_map.prev(op->start + op->len);
  if (liter != _sub_addresses_map.end()) {
    if (liter->second != riter->second) {
      for (auto iter = liter; iter != riter; iter++) {
        memory_operation_register(iter->second, op->op_id, REDSHOW_MEMORY_SET, true);
      }
    }
    memory_operation_register(riter->second, op->op_id, REDSHOW_MEMORY_SET, true);
  }
#endif

}


void MemoryLiveness::analysis_begin(
  u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 stream_id, u32 cubin_id, u32 mod_id,
  GPUPatchType type, void* trace_data
) {
  // Do not need to know value and need to get interval of memory
  assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

#ifdef REDSHOW_GPU_ANALYSIS
  gpu_patch_buffer_t* buffer = static_cast<gpu_patch_buffer_t*>(trace_data);

  if (buffer->aux) {
    update_aux_hit(buffer->aux, host_op_id);
    update_graph_at_kernel(buffer->aux, host_op_id, stream_id);
  }

  if (buffer->torch_aux) {
    update_aux_hit(buffer->torch_aux, host_op_id, true);
  }

#else

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
  _trace = std::dynamic_pointer_cast<MemoryLivenessTrace>(
    this->_kernel_trace[cpu_thread][host_op_id]
  );

  unlock();
#endif

}

void MemoryLiveness::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void MemoryLiveness::block_enter(const ThreadId &thread_id) {}

void MemoryLiveness::block_exit(const ThreadId &thread_id) {}

void MemoryLiveness::unit_access(
  i32 kernel_id, u64 host_op_id, const ThreadId &thread_id, const AccessKind &access_kind,
  const Memory &memory, u64 pc, u64 value, u64 addr, u32 index, GPUPatchFlags flags
) {
#ifndef REDSHOW_GPU_ANALYSIS
  if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  if (!_trace->access_memory.has(memory.op_id)) {
    _trace->access_memory.emplace(memory.op_id, true);
  }
  
#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
  auto iter = _sub_addresses_map.prev(memory.memory_range.start); 

  // memory_operation_register(riter->second, host_op_id, ACCESS, true);
  if (!_trace->access_submemory.has(iter->second)) {
    _trace->access_submemory.emplace(iter->second, true);
  }
#endif
#endif
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

  output << "memory_peak_kernel: " << _memory_peak_kernel << std::endl;
  output << "optimal_memory_peak: " << _optimal_memory_peak << " B" << std::endl;
  output << "current_memory_peak: " << _current_memory_peak - 512 << " B" << std::endl;
  output << std::endl;

  for (auto op : _op_node) {
    output << "op_id: " << op.first << ", ";
    auto ctx_type =_ctx_node.at(op.second);

    if (ctx_type == REDSHOW_MEMORY_ALLOC) {
      output << "ALLOC " << op.second << std::endl; 
    } else if (ctx_type == REDSHOW_MEMORY_FREE) {
      output << "FREE " << op.second << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_ACCESS) {
      output << "ACCESS " << op.second << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_SET) {
      output << "SET " << op.second << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_COPYT) {
      output << "COPYT " << op.second << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_COPYF) {
      output << "COPYF " << op.second << std::endl;
    } else if (ctx_type == REDSHOW_SUBMEMORY_ALLOC) {
      output << "SUBALLOC" << op.second << std::endl;
    } else if (ctx_type == REDSHOW_SUBMEMORY_FREE) {
      output << "SUBFREE" << op.second << std::endl;
    }

  }

  output.close();
}

void MemoryLiveness::output_stream_info(std::string file_name) {

  std::ofstream output(file_name);

  output << "test" << std::endl;

  for (auto op : _op_node) {
    output << "op_id: " << op.first << ", ";
    auto ctx_type =_ctx_node.at(op.second);
    auto op_stream = _op_to_stream.at(op.first);

    if (ctx_type == REDSHOW_MEMORY_ALLOC) {
      output << "ALLOC " << op.second << ", stream_id: " << op_stream.stream_id1 << std::endl; 
    } else if (ctx_type == REDSHOW_MEMORY_FREE) {
      output << "FREE " << op.second << ", stream_id: " << op_stream.stream_id1 << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_ACCESS) {
      output << "ACCESS " << op.second << ", stream_id: " << op_stream.stream_id1 << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_SET) {
      output << "SET " << op.second << ", stream_id: " << op_stream.stream_id1 << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_COPYT) {
      u32 stream_id;
      if (op_stream.op_type1 == REDSHOW_MEMORY_COPYT) {
        stream_id = op_stream.stream_id1;
      } else if (op_stream.op_type2 == REDSHOW_MEMORY_COPYT) {
        stream_id = op_stream.stream_id2;
      }
      output << "COPYT " << op.second << ", stream_id: " << stream_id << std::endl;
    } else if (ctx_type == REDSHOW_MEMORY_COPYF) {
      u32 stream_id;
      if (op_stream.op_type1 == REDSHOW_MEMORY_COPYF) {
        stream_id = op_stream.stream_id1;
      } else if (op_stream.op_type2 == REDSHOW_MEMORY_COPYF) {
        stream_id = op_stream.stream_id2;
      }
      output << "COPYF " << op.second << ", stream_id: " << stream_id << std::endl;
    } else if (ctx_type == REDSHOW_SUBMEMORY_ALLOC) {
      output << "SUBALLOC" << op.second << std::endl;
    } else if (ctx_type == REDSHOW_SUBMEMORY_FREE) {
      output << "SUBFREE" << op.second << std::endl;
    }

  }

  output.close();
}

void MemoryLiveness::output_memory_size_growth_sequence(std::string filename) {
  std::ofstream output(filename);
  output << "memory_peak_kernel: " << _memory_peak_kernel << std::endl;
  output << "optimal_memory_peak: " << _optimal_memory_peak << " B" << std::endl;
  output << "current_memory_peak: " << _current_memory_peak - 512
        << " B" << std::endl << std::endl;

  for (auto op : _memory_size_log) {
    output << op.first << "(" << op.second.op << "): " << op.second.size
          << " B" << std::endl;
  }
  output.close();
}

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS

void MemoryLiveness::output_submemory_liveness(std::string file_name) {
  if (_submemories.empty()) {
    return;
  }
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

  if (_submemories.empty()) {
    return;
  }

  // sort the vector
  for (int i = 0; i < _submemory_size_list.size()-1; i++) { // TODO(@Lin-Mao): unknown bug
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
  if (_submemories.empty()) {
    return;
  }

  std::ofstream output(file_name);
  output << "submemory_peak_kernel: " << _submemory_peak_kernel << std::endl;
  output << "optimal_submemory_peak: " << _optimal_submemory_peak << " B" << std::endl;
  output << "current_submemory_peak: " << _current_submemory_peak - 512
        << " B" << std::endl << std::endl;
  output.close();
}

void MemoryLiveness::output_submemory_size_growth_sequence(std::string filename) {
  if (_submemories.empty()) {
    return;
  }

  std::ofstream output(filename);
  output << "submemory_peak_kernel: " << _submemory_peak_kernel << std::endl;
  output << "optimal_submemory_peak: " << _optimal_submemory_peak << " B" << std::endl;
  output << "current_submemory_peak: " << _current_submemory_peak - 512
        << " B" << std::endl << std::endl;

  for (auto op : _submemory_size_log) {
    output << op.first << "(" << op.second.op << "): " << op.second.size << " B" << std::endl;
  }
  output.close();

  // Used for ls peak change
  std::ofstream output1("id_" + filename);
  output1 << "submemory_peak_kernel: " << _submemory_peak_kernel << std::endl;
  output1 << "optimal_submemory_peak: " << _optimal_submemory_peak << " B" << std::endl;
  output1 << "current_submemory_peak: " << _current_submemory_peak - 512
          << " B" << std::endl << std::endl;

  for (auto op : _submemory_size_log) {
    output1 << "(" << op.second.op << "): " << op.second.size << " B" << std::endl;
  }
  output1.close();
}

void MemoryLiveness::output_torch_python_states(std::string filename) {
  if (_submemories.empty()) {
    return;
  }

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

void MemoryLiveness::output_merged_torch_python_states(std::string filename) {
  if (_submemories.empty()) {
    return;
  }

  std::ofstream output(filename);
  
  for (
    auto liter = _torch_python_states.begin();
    liter != _torch_python_states.end();
    liter
  )
  {
    auto temp_iter = liter;
    output << "--------------------------------------------------" << std::endl;
    output << _sub_op_node.at(liter->first) << " " << liter->first << std::endl;
    for(auto riter = ++temp_iter; riter != _torch_python_states.end(); riter) {
      if (riter->second == liter->second) {
        output << _sub_op_node.at(riter->first) << " " << riter->first << std::endl;
        riter = _torch_python_states.erase(riter);
      } else {
        ++riter;
      }
    }
    int count = 0;
    for (auto state_iter : liter->second) {
      output << "(" << count << ")"
             << "File: " <<  state_iter.file_name << std::endl;
      output << "\tFunction: " << state_iter.function_name << std::endl;
      output << "\tFirst line: " << state_iter.function_first_lineno << std::endl;
      output << "\tCall at line: " << state_iter.lineno << std::endl;
      count++;
    }
    output << std::endl;
    liter = _torch_python_states.erase(liter);
  }

  output.close();
}

void MemoryLiveness::output_torch_libunwind_backtrace(std::string filename) {
  if (_submemories.empty()) {
    return;
  }

  std::ofstream output(filename);

  for (
    auto liter = _memory_libunwind_frames.begin();
    liter != _memory_libunwind_frames.end();
    liter
  )
  {
    auto temp_iter = liter;
    output << "--------------------------------------------------" << std::endl;
    output << _sub_op_node.at(liter->first) << " " << liter->first << std::endl;
    for (auto riter = ++temp_iter; riter != _memory_libunwind_frames.end(); riter) {
      if (riter->second == liter->second) {
        output << _sub_op_node.at(riter->first) << " " << riter->first << std::endl;
        riter = _memory_libunwind_frames.erase(riter);
      } else {
        ++riter;
      }
    }
    for (auto frame : liter->second) {
      output << "0x" << frame.pc << ": ";
      output << frame.frame << "+0x" << frame.offset << std::endl;
    }

    output << std::endl;
    liter = _memory_libunwind_frames.erase(liter);
  }

  output.close();
}

#endif

void MemoryLiveness::flush_thread(
  u32 cpu_thread,
  const std::string &output_dir,
  const LockableMap<u32, Cubin> &cubins,
  redshow_record_data_callback_func record_data_callback
) {}

void MemoryLiveness::flush(
  const std::string &output_dir,
  const LockableMap<u32, Cubin> &cubins,
  redshow_record_data_callback_func record_data_callback
) 
{

  output_memory_operation_list(output_dir + "memory_liveness.txt");

  output_memory_size_list(output_dir + "memory_size_list.txt");

  output_kernel_list(output_dir + "kernel_list.txt");

  output_ctx_node(output_dir + "memory_liveness.csv");

  output_memory_size_growth_sequence(output_dir + "memory_growth_sequence.txt");

  output_stream_info(output_dir + "operation_stream_info.txt");

  dump_dependency_graph(output_dir + "dependency_graph.dot");

  dump_topological_order(output_dir + "topological_order.txt");

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS

  output_submemory_liveness(output_dir + "submemory_liveness.txt");

  output_submemory_size_list(output_dir + "submemory_size_list.txt");

  output_submemory_info(output_dir + "submemory_info.txt");

  output_torch_python_states(output_dir + "torch_python_states.txt");

  output_merged_torch_python_states(output_dir + "merged_torch_python_states.txt");

  output_submemory_size_growth_sequence(output_dir + "submemory_growth_sequence.txt");

  output_torch_libunwind_backtrace(output_dir + "torch_libunwind_backtrace.txt");

#endif
}

void MemoryLiveness::update_global_op_id_start(u64 op_id) {
  if (op_id < _global_op_id_start) {
    _global_op_id_start = op_id;
  }
}

void MemoryLiveness::update_stream_nodes(u64 op_id, u32 stream_id) {
  auto stream_nodes = _stream_nodes.find(stream_id);
  if (stream_nodes == _stream_nodes.end()) {
    Vector<NodeIndex> vec;
    vec.push_back(op_id);
    _stream_nodes.emplace(stream_id, vec);
  } else {
    auto node = std::find(stream_nodes->second.begin(), stream_nodes->second.end(), op_id);
    if (node == stream_nodes->second.end()) {
      stream_nodes->second.push_back(op_id);
    }
  }
}

void MemoryLiveness::update_graph_at_malloc(u64 op_id, u32 stream_id, memory_operation_t op_type) {
  // Assume the stream id of cudaMalloc is 0
  if (!_graph.has_node(op_id)) {
    _graph.add_node(op_id, Node(op_id, op_type));
    _obj_ownership_map.emplace(op_id, op_id);
  }
  update_stream_nodes(op_id, stream_id);

  auto prev_stream_node = _stream_ownership_map.find(stream_id);
  if (prev_stream_node == _stream_ownership_map.end()) {
    _stream_ownership_map.emplace(stream_id, op_id);
  } else {
    auto edge_index = EdgeIndex(prev_stream_node->second, op_id, STREAM_DEPENDENCY);
    _graph.add_edge(edge_index, Edge());
    _edge_list.push_back(edge_index);
    prev_stream_node->second = op_id;
  }
}

void MemoryLiveness::update_graph_at_free(u64 op_id, u32 stream_id, u64 memory_op_id, memory_operation_t op_type) {
  if (!_graph.has_node(op_id)) {
    _graph.add_node(op_id, Node(op_id, op_type));
  }
  auto & owner = _obj_ownership_map.at(memory_op_id);
  // auto edge_index = EdgeIndex(index, op_id, DATA_DEPENDENCY);
  // index = op_id;
  // _graph.add_edge(edge_index, Edge(memory_op_id, WRITE));
  // _edge_list.push_back(edge_index);

  auto & read_nodes = _read_nodes.at(memory_op_id);
  if (read_nodes.empty()) {
    auto edge_index = EdgeIndex(owner, op_id, DATA_DEPENDENCY);
    _graph.add_edge(edge_index, Edge(memory_op_id, WRITE));
    _edge_list.push_back(edge_index);
  } else {
    for (auto rnode : read_nodes) {
      auto edge_index = EdgeIndex(rnode, op_id, DATA_DEPENDENCY);
      _graph.add_edge(edge_index, Edge(memory_op_id, WRITE));
      _edge_list.push_back(edge_index);
    }
    read_nodes.clear();
  }

  owner = op_id;
  

  update_stream_nodes(op_id, stream_id);

  auto prev_stream_node = _stream_ownership_map.find(stream_id);
  if (prev_stream_node == _stream_ownership_map.end()) {
    _stream_ownership_map.emplace(stream_id, op_id);
  } else {
    auto edge_index = EdgeIndex(prev_stream_node->second, op_id, STREAM_DEPENDENCY);
    _graph.add_edge(edge_index, Edge());
    _edge_list.push_back(edge_index);
    prev_stream_node->second = op_id;
  }
}

void MemoryLiveness::update_graph_at_access(u64 op_id, memory_operation_t op_type, u32 stream_id,
                                            u64 obj_op_id, AccessType access_type) {
  if (!_graph.has_node(op_id)) {
    _graph.add_node(op_id, Node(op_id, op_type));
  }
  // For stream dependency
  auto prev_stream_node = _stream_ownership_map.find(stream_id);
  if (prev_stream_node == _stream_ownership_map.end()) {
    _stream_ownership_map.emplace(stream_id, op_id);
  } else {
    if (prev_stream_node->second != op_id) { // in case kernel call multiple times
      auto edge_index = EdgeIndex(prev_stream_node->second, op_id, STREAM_DEPENDENCY);
      _graph.add_edge(edge_index, Edge());
      _edge_list.push_back(edge_index);
      prev_stream_node->second = op_id;
    }
    
  }

  update_stream_nodes(op_id, stream_id);

  // For data dependency
  // auto prev_obj_node = _obj_ownership_map.find(obj_op_id);
  // auto edge_index = EdgeIndex(prev_obj_node->second, op_id, DATA_DEPENDENCY);
  // if (_graph.has_edge(edge_index)) {
  //   auto & edge = _graph.edge(edge_index);
  //   edge.touched_objs.emplace(obj_op_id, access_type);
  // } else {
  //   _graph.add_edge(edge_index, Edge(obj_op_id, access_type));
  //   _edge_list.push_back(edge_index);
  // }

  // if (access_type == WRITE) {
  //   prev_obj_node->second = op_id;
  // }

  // For data dependency
  auto prev_obj_node = _obj_ownership_map.find(obj_op_id);
  
  if (access_type == READ) {
    auto & read_nodes = _read_nodes.at(obj_op_id);
    read_nodes.push_back(op_id);
    auto edge_index = EdgeIndex(prev_obj_node->second, op_id, DATA_DEPENDENCY);
    if (_graph.has_edge(edge_index)) {
      auto & edge = _graph.edge(edge_index);
      edge.touched_objs.emplace(obj_op_id, access_type);
    } else {
      _graph.add_edge(edge_index, Edge(obj_op_id, access_type));
      _edge_list.push_back(edge_index);
    }
  }

  else if (access_type == WRITE) {
    auto & read_nodes = _read_nodes.at(obj_op_id);
    if (read_nodes.empty()) {
      auto edge_index = EdgeIndex(prev_obj_node->second, op_id, DATA_DEPENDENCY);
      if (_graph.has_edge(edge_index)) {
        auto & edge = _graph.edge(edge_index);
        edge.touched_objs.emplace(obj_op_id, access_type);
      } else {
        _graph.add_edge(edge_index, Edge(obj_op_id, access_type));
        _edge_list.push_back(edge_index);
      }
    } else {
      for (auto rnode : read_nodes) {
        auto edge_index = EdgeIndex(rnode, op_id, DATA_DEPENDENCY);
        if (_graph.has_edge(edge_index)) {
          auto & edge = _graph.edge(edge_index);
          edge.touched_objs.emplace(obj_op_id, access_type);
        } else {
          _graph.add_edge(edge_index, Edge(obj_op_id, access_type));
          _edge_list.push_back(edge_index);
        }
      }
      read_nodes.clear();
    }

    prev_obj_node->second = op_id;
  }

  else {
    printf("It should not take this branch.\n");
  }
}

void MemoryLiveness::update_graph_at_kernel(void* aux, u64 op_id, u32 stream_id) {
  gpu_patch_aux_address_dict_t* kernel_aux = static_cast<gpu_patch_aux_address_dict_t*>(aux);

  for (int i = 0; i < kernel_aux->size; i++) {
    if (kernel_aux->hit[i] == 1) {
      u64 memory_op_id = _addresses_map.at(kernel_aux->start_end[i].start);
      if (kernel_aux->write[i] == 1) {
        update_graph_at_access(op_id, REDSHOW_MEMORY_ACCESS, stream_id, memory_op_id, WRITE);
      } else if (kernel_aux->read[i] == 1) {
        update_graph_at_access(op_id, REDSHOW_MEMORY_ACCESS, stream_id, memory_op_id, READ);
      } else {
        printf("It shouldn't take this branch!!!\n");
      }
    }
  }
}

void MemoryLiveness::dump_dependency_graph(std::string filename) {
  std::ofstream output(filename);
  output << "digraph G {" << std::endl;
  size_t count = 0;
  Vector<NodeIndex> alloc_nodes;
  Vector<NodeIndex> free_nodes;
  Vector<NodeIndex> other_nodes;
  Map<u64, std::string> node_labels;
  // output << "_stream_nodes: " << _stream_nodes.size() << std::endl;
  for (auto nodes : _stream_nodes) {
    output << "subgraph cluster_" << count << " {" << std::endl;
    output << "style=rounded;" << std::endl;
    output << "color=lightgrey;" << std::endl;
    // output << "node [style=filled];" << std::endl;
    output << "label = \"stream" << nodes.first << "\";" << std::endl;
    for (auto node : nodes.second) {
      auto id = node - _global_op_id_start;
      output << id;
      if (node != nodes.second.back()) {
        output << " -> ";
      }
      if (_graph.node(node).op_type == REDSHOW_MEMORY_ALLOC) {
        alloc_nodes.push_back(node);
        node_labels.emplace(id, "A"+std::to_string(id));
      } else if (_graph.node(node).op_type == REDSHOW_MEMORY_FREE) {
        free_nodes.push_back(node);
        node_labels.emplace(id, "F"+std::to_string(id));
      } else if (_graph.node(node).op_type == REDSHOW_MEMORY_SET) {
        other_nodes.push_back(node);
        node_labels.emplace(id, "S"+std::to_string(id));
      } else if (_graph.node(node).op_type == REDSHOW_MEMORY_ACCESS) {
        other_nodes.push_back(node);
        node_labels.emplace(id, "K"+std::to_string(id));
      } else if (_graph.node(node).op_type == REDSHOW_MEMORY_COPYT
                || _graph.node(node).op_type == REDSHOW_MEMORY_COPYF) {
        other_nodes.push_back(node);
        node_labels.emplace(id, "C"+std::to_string(id));
      }
    }
    output << "[color=green];" << std::endl;
    for (auto node : alloc_nodes) {
      auto id = node - _global_op_id_start;
      output << id << "[shape=box,label=" << node_labels[id] << "];" << std::endl;
    }
    for (auto node : free_nodes) {
      auto id = node - _global_op_id_start;
      output << id << "[shape=box,color=lightgrey,style=filled,label=" << node_labels[id] << "];" << std::endl;
    }
    for (auto node : other_nodes) {
      auto id = node - _global_op_id_start;
      output << id << "[label=" << node_labels[id] << "];" << std::endl;
    }
    alloc_nodes.clear();
    free_nodes.clear();
    other_nodes.clear();
    node_labels.clear();

    output << "}" << std::endl;
    count++;
  }

  for (auto edge_index : _edge_list) {
    if (edge_index.edge_type == DATA_DEPENDENCY) {
      output << edge_index.from - _global_op_id_start << " -> " << edge_index.to - _global_op_id_start;

      // get edge label
      auto edge = _graph.edge(edge_index);
      auto objs = edge.touched_objs;
      // std::string str = "";
      // if (!objs.empty()) {
      //   if (_graph.node(edge_index.to).op_type == REDSHOW_MEMORY_FREE) {
      //     str += "D" + (objs.begin()->first - _global_op_id_start);
      //   } else {
      //     for (auto obj : objs) {
      //       if (obj.second == READ) {
      //         str += "R" + (obj.first - _global_op_id_start);
      //       } else {
      //         str += "W" + (obj.first - _global_op_id_start);
      //       }
      //       str += " ";
      //     }
      //   }
      // }
      // output << "[color=red,label=\"" << str << "\"];" << std::endl;
      output << "[color=red,label=\"";
      if (!objs.empty()) {
        if (_graph.node(edge_index.to).op_type == REDSHOW_MEMORY_FREE) {
          output << "D" << (objs.begin()->first - _global_op_id_start);
        } else {
          for (auto obj : objs) {
            if (obj.second == READ) {
              output << "R" << (obj.first - _global_op_id_start);
            } else {
              output << "W" << (obj.first - _global_op_id_start);
            }
            output << " ";
          }
        }
      }
      output << "\"];" << std::endl;
    }
  }  

  output << "}" << std::endl;
  output.close();
}

void MemoryLiveness::dump_topological_order(std::string filename) {
  u64 top_index = 0;
  Map<u64, Vector<u64>> topological_index;

  _graph.dump_graph(_global_op_id_start);
  
  while (!_graph.is_empty()) {
    auto nodes = _graph.get_no_inedge_nodes();
    for (auto node : nodes) {
      _graph.remove_node(node);
    }
    topological_index.emplace(top_index, nodes);
    top_index++;
  }

  std::ofstream output(filename);
  for (auto i : topological_index) {
    output << "top_index: " << i.first << std::endl;
    for (auto j : i.second) {
      output << j << " " << j - _global_op_id_start << std::endl;
    }
    output << std::endl;
  }

  output.close();
}

}   // namespace redshow