/**
 * @file memory_profile.cpp
 * @author @Lin-Mao
 * @brief New mode in GVPorf for memory profiling
 * @version 0.1
 * @date 2021-09-28
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include "analysis/memory_profile.h"



// #define DEBUG_PRINT

// #define SUB_MEMORY

#include <iostream>
#ifdef DEBUG_PRINT
#include <iostream>
#endif

namespace redshow {

void MemoryProfile::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}

void MemoryProfile::memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op) {
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

void MemoryProfile::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory /* default = false */) {
  update_op_node(op->op_id, op->ctx_id);

  if (op->op_id < _first_alloc_op_id)
    _first_alloc_op_id = op->op_id;

  if (!is_submemory) {
    
    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);
    
    memory_operation_register(op->op_id, op->op_id, ALLOC);

    // TODO(@Lin-Mao): This map can be removed, to design a better update_chunk function
    larget_chunk_with_memories.try_emplace(op->op_id, op->len);


    // uint8_t* arr = (uint8_t*) malloc(sizeof(uint8_t) * op->len);
    // memset(arr, 0, sizeof(uint8_t) * op->len);
  
    // HeatMapMemory heatmap(op->len);
    // heatmap.array = arr;
    // _heatmap_list[op->op_id] = heatmap;

  } else { // is_submemory == true
    
    _sub_memories.try_emplace(op->op_id, op);
  }
 
}

void MemoryProfile::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory /* default = false */) {
  update_op_node(op->op_id, op->ctx_id);
  
  if (op->op_id > _last_free_op_id)
    _last_free_op_id = op->op_id;
  
  if (!is_submemory) {

    u64 address = op->memory_range.start;
    u64 malloc_op_id = _addresses_map.at(address); 
    _current_memories.erase(malloc_op_id);
    _addresses_map.erase(address);
    _liveness_map.emplace(malloc_op_id, op->op_id);

    memory_operation_register(malloc_op_id, op->op_id, FREE);

  } else {
    // TODO(@Lin-Mao)
  }
}

void MemoryProfile::memcpy_op_callback(std::shared_ptr<Memcpy> op) {
  update_op_node(op->op_id, op->ctx_id);
  
  if (op->src_memory_op_id != REDSHOW_MEMORY_HOST) {
    memory_operation_register(op->src_memory_op_id, op->op_id, COPYF);
  }

  if (op->dst_memory_op_id != REDSHOW_MEMORY_HOST) {
    memory_operation_register(op->dst_memory_op_id, op->op_id, COPYT);
  }

  
}

void MemoryProfile::memset_op_callback(std::shared_ptr<Memset> op) {
  update_op_node(op->op_id, op->ctx_id);

  memory_operation_register(op->memory_op_id, op->op_id, SET);
}


void MemoryProfile::update_blank_chunks(u64 kernel_op_id, u64 memory_op_id, MemoryRange range_iter) {

  auto &memory_map = _blank_chunks.at(kernel_op_id);
  auto &unuse_range_set = memory_map.at(memory_op_id);

  auto start = range_iter.start;
  auto end = range_iter.end;


  // full access
  if (unuse_range_set.size() == 0) {
    return;
  }

  // return the iter in set when iter.start < = range_iter.start
  auto iter = unuse_range_set.prev(range_iter); 

  bool finished = false;

  while(!finished) {
    if (iter != unuse_range_set.end()) {
      if (start == iter->start) {
        if (end < iter->end) {
          start = iter->end; // new end of the unuse range
          iter = unuse_range_set.erase(iter);
          unuse_range_set.emplace_hint(iter, end, start);
          return;

        } else { // end >= iter->end
          iter = unuse_range_set.erase(iter);
          if (iter == unuse_range_set.end()) {
            return;
          }

          if (end < iter->start) {
            return;

          } else { // end >= iter->start
            start = iter->start;
            finished = false;
          }
        }
      
      } else { // iter->start < start

        if (start < iter->end) {
          auto temp_iter_start = iter->start;
          auto temp_iter_end = iter->end;
          iter = unuse_range_set.erase(iter);
          unuse_range_set.emplace(temp_iter_start, start);
          iter = unuse_range_set.emplace_hint(iter, start, temp_iter_end);// start == iter->start
          finished = false;
        } else { // start >= iter->end;
          iter++;
          if (iter == unuse_range_set.end()) {
            return; // traverse all range
          }
          
          if (end > iter->start) {
            start = iter->start;
            finished = false;
          } else { // end <= iter->start
            return;
          }
        } 

      }

    } else { // iter == unuse_range_set.end,
      iter = unuse_range_set.begin();
      if (end <= iter->start) {
        return;
        
      } else { // end > unuse_range_set.begin()->start
        start = iter->start;
        finished = false;
      } 
    }
  }

}


void MemoryProfile::update_object_fragmentation_in_kernel(u32 cpu_thread, u64 kernel_op_id) {
  // KernelOpPair kop = KernelOpPair(kernel_id, op_id);
  auto unused_map = _blank_chunks.at(kernel_op_id);

  
  
  for (auto &miter : unused_map) {
    size_t len = 0; // the largest blank size
    size_t sum = 0; // total blank size
    bool empty_set = true;

    // miter == Map<u64, Set<MemoryRange>>
    for (auto &siter : miter.second) {
      empty_set = false;
      sum += siter.end - siter.start;
      if (len < (siter.end - siter.start)) {
        len = siter.end - siter.start;
      }
    }

    // repair largest chunk increase acrossing kernel
    if (len > larget_chunk_with_memories.at(miter.first)) {
      len = larget_chunk_with_memories.at(miter.first);
    }
    if (len == 0) {
      empty_set = true;
    }
    larget_chunk_with_memories[miter.first] = len;
    
    if (!empty_set) {
      float frag = 1 - (float) len / sum;
      ChunkFragmentation chunck_frag(len, frag);
      _object_fragmentation_of_kernel_per_thread[cpu_thread][kernel_op_id][miter.first] = chunck_frag;
    } else {
      ChunkFragmentation chunck_frag(len, 0.0);
      _object_fragmentation_of_kernel_per_thread[cpu_thread][kernel_op_id][miter.first] = chunck_frag;
    }

  }  

}


void MemoryProfile::kernel_op_callback(std::shared_ptr<Kernel> op) {
  
  update_op_node(op->op_id, op->ctx_id);

  size_t size = 0;
  for (auto iter : _current_memories) {
    size += iter.second->len;
  }
  _kernel_memory_size.emplace(op->op_id, size);
  
  if (_trace.get() == NULL) {
    // If the kernel is sampled
    return;
  }

  Map<u64, Set<MemoryRange>> initail_unuse_memory_map;
  _blank_chunks.emplace(_trace->kernel.op_id, initail_unuse_memory_map);
  
  auto &unuse_memory_map_in_kernel = _blank_chunks[_trace->kernel.op_id];

// for read access trace
  for (auto &mem_iter : _trace->read_memory) {
    memory_operation_register(mem_iter.first, _trace->kernel.op_id, ACCESS);

#ifdef SUB_MEMORY
    auto memory = _sub_memories.at(mem_iter.first);
#endif

#ifndef SUB_MEMORY
    auto memory = _memories.at(mem_iter.first);
#endif
    // Set<MemoryRange> mem_unuse_set;
    // mem_unuse_set.insert(memory->memory_range);
    // unuse_memory_map_per_kernel.emplace(mem_iter.first, mem_unuse_set);


    if (_accessed_memories.find(mem_iter.first) != _accessed_memories.end()) {
      auto kid = _accessed_memories.at(mem_iter.first);
      auto mem_unuse_set = _blank_chunks.at(kid).at(mem_iter.first);
      unuse_memory_map_in_kernel.emplace(mem_iter.first, mem_unuse_set);
      // ensure the lastest mem_unuse_set
      _accessed_memories[mem_iter.first] = _trace->kernel.op_id;
    } else {
      _accessed_memories.emplace(mem_iter.first, _trace->kernel.op_id);
      Set<MemoryRange> mem_unuse_set;
      mem_unuse_set.insert(memory->memory_range);
      unuse_memory_map_in_kernel.emplace(mem_iter.first, mem_unuse_set);
    }

    if (memory->op_id > REDSHOW_MEMORY_HOST) {
      // get op_id and ctx_id
      auto node_id = _op_node.at(memory->op_id);
      // auto len = 0;
       if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {

      int r_count = 0; // for debug count
      // mem_iter.second is a Set<MemRange>
      for (auto &range_iter : mem_iter.second) {


        // len += range_iter.end - range_iter.start;
        // update_blank_chunks(_trace->kernel.op_id, mem_iter.first, range_iter);
      }  
        
      } else {
        // len = memory->len;
        (void) 0;
      }
    }  
  }

// for write access trace
  for (auto &mem_iter : _trace->write_memory) {
    memory_operation_register(mem_iter.first, _trace->kernel.op_id, ACCESS);

#ifdef SUB_MEMORY
    auto memory = _sub_memories.at(mem_iter.first);
#endif

#ifndef SUB_MEMORY
    auto memory = _memories.at(mem_iter.first);
#endif

    if (_accessed_memories.find(mem_iter.first) != _accessed_memories.end()) {
      auto kid = _accessed_memories.at(mem_iter.first);
      auto mem_unuse_set = _blank_chunks.at(kid).at(mem_iter.first);
      unuse_memory_map_in_kernel.emplace(mem_iter.first, mem_unuse_set);
      // ensure the lastest mem_unuse_set
      _accessed_memories[mem_iter.first] = _trace->kernel.op_id;
    } else {
      _accessed_memories.emplace(mem_iter.first, _trace->kernel.op_id);
      Set<MemoryRange> mem_unuse_set;
      mem_unuse_set.insert(memory->memory_range);
      unuse_memory_map_in_kernel.emplace(mem_iter.first, mem_unuse_set);
    }

    if (memory->op_id > REDSHOW_MEMORY_HOST) {
      // get op_id and ctx_id
      auto node_id = _op_node.at(memory->op_id);
       if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
         


    int w_count = 0; // for debug count
    // mem_iter.second is a Set<MemRange>
    for (auto &range_iter : mem_iter.second) {
      

        // len += range_iter.end - range_iter.start;
        // update_blank_chunks(_trace->kernel.op_id, mem_iter.first, range_iter);
    }  
      } else {
        // len = memory->len;
        (void) 0;
      }
    }
  }

    update_object_fragmentation_in_kernel(_trace->kernel.cpu_thread, _trace->kernel.op_id);

  // reset _trace
  _trace->read_memory.clear();
  _trace->write_memory.clear();
  _trace = NULL;
}


void MemoryProfile::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
// TODO(@Mao):
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


void MemoryProfile::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id, 
                                    GPUPatchType type, void* aux) {
    // Do not need to know value and need to get interval of memory
    assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

    lock();

    if (!this->_kernel_trace[cpu_thread].has(host_op_id)) {
    auto trace = std::make_shared<MemoryProfileTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    trace->kernel.op_id = host_op_id;
    this->_kernel_trace[cpu_thread][host_op_id] = trace;
    }

    _trace = std::dynamic_pointer_cast<MemoryProfileTrace>(this->_kernel_trace[cpu_thread][host_op_id]);

    unlock();
}


void MemoryProfile::analysis_end(u32 cpu_thread, i32 kernel_id) {

}


void MemoryProfile::block_enter(const ThreadId &thread_id) {

}


void MemoryProfile::block_exit(const ThreadId &thread_id) {

}


void MemoryProfile::merge_memory_range(Set<MemoryRange> &memory, const MemoryRange &memory_range) {
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


// not a whole buffer, but a part buffer in a memory object
void MemoryProfile::unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags) {

if (memory.op_id <= REDSHOW_MEMORY_HOST) {
    return;
  }

  auto &memory_range = memory.memory_range;

  


  if (flags & GPU_PATCH_READ) {
    
    if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
      merge_memory_range(_trace->read_memory[memory.op_id], memory_range);
    } else if (_trace->read_memory[memory.op_id].empty()) {
      _trace->read_memory[memory.op_id].insert(memory_range);
    }
  }
  if (flags & GPU_PATCH_WRITE) {

    merge_memory_range(_trace->write_memory[memory.op_id], memory_range);
  }

}

  // Flush
void MemoryProfile::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {
  
  // std::ofstream out(output_dir + "memory_profile_thread_flush" + std::to_string(cpu_thread) + ".csv");

  // out << "This is flush thread test." << std::endl;
  // out.close();

}

void MemoryProfile::output_memory_size_list(std::string file_name) {
  for (auto iter : _memories) {
    _memory_size_list.push_back(MemoryEntry(iter.first, iter.second->len));
  }
  // for (auto iter : _memory_size_list)
  //   std::cout << "op_id=" << iter.op_id << ", size=" << iter.size << std::endl;

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

void MemoryProfile::output_memory_operation_list(std::string file_name) {
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


void MemoryProfile::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {

  output_memory_size_list(output_dir + "largest_memory_list.txt");

  output_memory_operation_list(output_dir + "memory_operation_list.txt");
  
  std::ofstream out(output_dir + "memory_profile_flush" + ".csv");

  out << "small op id: " << _first_alloc_op_id << " large op id: " << _last_free_op_id << std::endl;
  size_t sum = 0; 
  for (auto iter : _kernel_memory_size) {
    sum += iter.second;
  }
  out << "Launch " << _kernel_memory_size.size() << " kernels, " ;
  if (_kernel_memory_size.size() != 0) {
    out << "average memory usage in each kernel launch: " << sum / _kernel_memory_size.size() << " B" << std::endl;
  } else {
    out << "average memory usage in each kernel launch: 0 B" << std::endl;
  }
     
      

  // <thread_id, <kernel_op_id, <memory_op_id, fragmentation>>>>
  for (auto &titer : _object_fragmentation_of_kernel_per_thread) {
    out << "Thread_id " << titer.first << std::endl;
    auto &kernel_map = _object_fragmentation_of_kernel_per_thread.at(titer.first);
    for (auto &kiter : kernel_map) {
      out << "  Kernel launched at " << kiter.first << " kernel_id " << _op_node[kiter.first] << std::endl;
      out << "  mem_peak: " << _kernel_memory_size.at(kiter.first) << " B" << std::endl;
      auto &memory_map = kernel_map.at(kiter.first);
      for (auto &miter : memory_map) {

#ifdef SUB_MEMORY
        auto memory = _sub_memories.at(miter.first);
#endif

#ifndef SUB_MEMORY
        auto memory = _memories.at(miter.first);
#endif 

        out << "    memory_op_id " << miter.first << std::endl;
        out << "    memory_id " << _op_node.at(miter.first) << std::endl;
        out << "    |- size " << memory->memory_range.end - memory->memory_range.start << " B" << std::endl;
        out << "    |- allocated at " << miter.first << std::endl;

        auto iter = _liveness_map.find(miter.first);
        if (iter == _liveness_map.end()) {
          out << "    |- freed at execution finished " << _last_free_op_id << std::endl;
        } else {
          out << "    |- freed at " << iter->second << " free_id " << _op_node.at(iter->second) << std::endl;
        }

        out << "    |- fragmentation " << miter.second.fragmentation << std::endl;
        out << "    |- largest chunk " << miter.second.largest_chunk << " B" << std::endl;
        out << "    |- unused memory " << 1 << std::endl;
        out << std::endl;
        
      }
      out << std::endl;
    }
    out << std::endl;
  }

  out.close();

}

}   // namespace redshow 