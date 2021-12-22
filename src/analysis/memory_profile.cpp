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

namespace redshow {

void MemoryProfile::update_op_node(u64 op_id, i32 ctx_id) {
  if (op_id > REDSHOW_MEMORY_HOST) {
    // Point the operation to the calling context
    _op_node[op_id] = ctx_id;
  }
}


void MemoryProfile::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory /* default = false */) {
  update_op_node(op->op_id, op->ctx_id);

  _memories.try_emplace(op->op_id, op);
  larget_chunk_with_memories.try_emplace(op->op_id, op->len);

  // TODO(@Mao): to store the _memories which is logging the allocated memory. Think about how to update it.

}


void MemoryProfile::update_blank_chunks(i32 kernel_id, u64 memory_op_id, MemoryRange range_iter) {

  auto &memory_map = _blank_chunks.at(kernel_id);
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


void MemoryProfile::update_object_fragmentation_in_kernel(u32 cpu_thread, i32 kernel_id) {
  // KernelOpPair kop = KernelOpPair(kernel_id, op_id);
  auto unused_map = _blank_chunks.at(kernel_id);

  
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
      _object_fragmentation_of_kernel_per_thread[cpu_thread][kernel_id][miter.first] = chunck_frag;
    } else {
      ChunkFragmentation chunck_frag(len, 0.0);
      _object_fragmentation_of_kernel_per_thread[cpu_thread][kernel_id][miter.first] = chunck_frag;
    }

  }  

}


void MemoryProfile::kernel_op_callback(std::shared_ptr<Kernel> op) {
  if (_trace.get() == NULL) {
    // If the kernel is sampled
    return;
  }

  Map<u64, Set<MemoryRange>> initail_unuse_memory_map;
  _blank_chunks.emplace(_trace->kernel.ctx_id, initail_unuse_memory_map);
  
  auto &unuse_memory_map_per_kernel = _blank_chunks[_trace->kernel.ctx_id];

// for read access trace
  for (auto &mem_iter : _trace->read_memory) {
    auto memory = _memories.at(mem_iter.first);

    if (_accessed_memories.find(mem_iter.first) != _accessed_memories.end()) {
      auto kid = _accessed_memories.at(mem_iter.first);
      auto mem_unuse_set = _blank_chunks.at(kid).at(mem_iter.first);
      unuse_memory_map_per_kernel.emplace(mem_iter.first, mem_unuse_set);
      // ensure the lastest mem_unuse_set
      _accessed_memories[mem_iter.first] = _trace->kernel.ctx_id;
    } else {
      _accessed_memories.emplace(mem_iter.first, _trace->kernel.ctx_id);
      Set<MemoryRange> mem_unuse_set;
      mem_unuse_set.insert(memory->memory_range);
      unuse_memory_map_per_kernel.emplace(mem_iter.first, mem_unuse_set);
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
        update_blank_chunks(_trace->kernel.ctx_id, mem_iter.first, range_iter);
      }  
        
      } else {
        // len = memory->len;
        (void) 0;
      }
    }  
  }

// for write access trace
  for (auto &mem_iter : _trace->write_memory) {
    auto memory = _memories.at(mem_iter.first);

    if (_accessed_memories.find(mem_iter.first) != _accessed_memories.end()) {
      auto kid = _accessed_memories.at(mem_iter.first);
      auto mem_unuse_set = _blank_chunks.at(kid).at(mem_iter.first);
      unuse_memory_map_per_kernel.emplace(mem_iter.first, mem_unuse_set);
      // ensure the lastest mem_unuse_set
      _accessed_memories[mem_iter.first] = _trace->kernel.ctx_id;
    } else {
      _accessed_memories.emplace(mem_iter.first, _trace->kernel.ctx_id);
      Set<MemoryRange> mem_unuse_set;
      mem_unuse_set.insert(memory->memory_range);
      unuse_memory_map_per_kernel.emplace(mem_iter.first, mem_unuse_set);
    }

    if (memory->op_id > REDSHOW_MEMORY_HOST) {
      // get op_id and ctx_id
      auto node_id = _op_node.at(memory->op_id);
       if (_configs[REDSHOW_ANALYSIS_READ_TRACE_IGNORE] == false) {
         


    int w_count = 0; // for debug count
    // mem_iter.second is a Set<MemRange>
    for (auto &range_iter : mem_iter.second) {

        // len += range_iter.end - range_iter.start;
        update_blank_chunks(_trace->kernel.ctx_id, mem_iter.first, range_iter);
    }  
      } else {
        // len = memory->len;
        (void) 0;
      }
    }
  }
    update_object_fragmentation_in_kernel(_trace->kernel.cpu_thread, _trace->kernel.ctx_id);

  // reset _trace
  _trace->read_memory.clear();
  _trace->write_memory.clear();
  _trace = NULL;
}


void MemoryProfile::op_callback(OperationPtr op) {
// TODO(@Mao):
  lock();
  
  if (op->type == OPERATION_TYPE_KERNEL) {
    kernel_op_callback(std::dynamic_pointer_cast<Kernel>(op));
  } else if (op->type == OPERATION_TYPE_MEMORY) {
    memory_op_callback(std::dynamic_pointer_cast<Memory>(op));
  }

  unlock();
}


void MemoryProfile::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id, GPUPatchType type) {
    // Do not need to know value and need to get interval of memory
    assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

    lock();

    if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<MemoryProfileTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
    }

    _trace = std::dynamic_pointer_cast<MemoryProfileTrace>(this->_kernel_trace[cpu_thread][kernel_id]);

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

void MemoryProfile::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {

  std::ofstream out(output_dir + "memory_profile_flush" + ".csv");

  // out << "************************************************************" << std::endl;
  // out << "******************** Fragmentation Info ********************" << std::endl;
  // out << "************************************************************" << std::endl;

  // <thread_id, <kernel_op_id, <memory_op_id, fragmentation>>>>
  for (auto &titer : _object_fragmentation_of_kernel_per_thread) {
    out << "Thread_id: " << titer.first << std::endl;
    auto &kernel_map = _object_fragmentation_of_kernel_per_thread.at(titer.first);
    for (auto &kiter : kernel_map) {
      out << "  Kernel_id: " << kiter.first << std::endl;
      auto &memory_map = kernel_map.at(kiter.first);
      for (auto &miter : memory_map) {
        auto memory = _memories.at(miter.first);
        out << "ctx_id" << std::endl;
        out << _op_node.at(miter.first) << std::endl;
        out << "    Memory[" << miter.first << "] (" << memory->memory_range.end - memory->memory_range.start 
        << " B) = " << miter.second.fragmentation << " (largest_chunk: " << miter.second.largest_chunk << " B)" << std::endl; 
      }
      out << std::endl;
    }
    out << std::endl;
  }
  // out << "************************************************************" << std::endl;
  // out << "****************** Fragmentation Info END ******************" << std::endl;
  // out << "************************************************************" << std::endl;
  out.close();

}

}   // namespace redshow 