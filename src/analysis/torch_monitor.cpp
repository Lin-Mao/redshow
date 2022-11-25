#include "analysis/torch_monitor.h"

#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <string>

#include "common/utils.h"


namespace redshow {


void TorchMonitor::op_callback(OperationPtr op, bool is_submemory /* default = false */) {
  // Add a calling context node
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

void TorchMonitor::memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory) {

  if (!is_submemory) {
    _memories.try_emplace(op->op_id, op);
    _current_memories.try_emplace(op->op_id, op);
    _addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _current_memory_usage += op->len;
    _nums_cudamalloc++;

    // printf("op_id:%lu, len:%d\n", op->op_id, op->len);
    if (_current_memory_usage > _memory_peak)
      _memory_peak = _current_memory_usage;
  } else {
    _submemories.try_emplace(op->op_id, op);
    _current_submemories.try_emplace(op->op_id, op);
    _sub_addresses_map.try_emplace(op->memory_range.start, op->op_id);
    _current_submemory_usage += op->len;

    if (_current_submemory_usage > _submemory_peak) {
      _submemory_peak = _current_submemory_usage;
    }
  }
}

void TorchMonitor::memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory) {
  // update_global_op_id_start(op->op_id);

  if (!is_submemory) {
    u64 address = op->memory_range.start;
    u64 malloc_op_id = _addresses_map.at(address);
    
    _addresses_map.erase(address);
    _current_memories.erase(malloc_op_id);
    _current_memory_usage -= op->len;
    _nums_cudafree++;

  } else {
    u64 address = op->memory_range.start;
    u64 sub_alloc_id = _sub_addresses_map.at(address);

    _sub_addresses_map.erase(address);
    _current_submemories.erase(sub_alloc_id);
    _current_submemory_usage -= op->len;
  }
}

void TorchMonitor::kernel_op_callback(std::shared_ptr<Kernel> op) {

}

void TorchMonitor::memcpy_op_callback(std::shared_ptr<Memcpy> op) {

}

void TorchMonitor::memset_op_callback(std::shared_ptr<Memset> op) {

}

void TorchMonitor::analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 stream_id,
                            u32 cubin_id, u32 mod_id, GPUPatchType type, void* trace_data) {
  assert(type == GPU_PATCH_TYPE_ADDRESS_PATCH || type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS);

  lock();

  gpu_patch_buffer_t* buffer = static_cast<gpu_patch_buffer_t*>(trace_data);

  if (buffer->aux) {
    update_aux_hit(buffer->aux, host_op_id);
  }

  if (buffer->torch_aux) {
    update_aux_hit(buffer->torch_aux, host_op_id, true);
  }

  unlock();
}

void TorchMonitor::update_aux_hit(void* aux, u64 kernel_op_id, bool is_sub) {
  gpu_patch_aux_address_dict_t* kernel_aux = static_cast<gpu_patch_aux_address_dict_t*>(aux);

  if (!is_sub) {
    u64 kernel_memory_usage = 0;
    for (int i = 0; i < kernel_aux->size; i++) {
      if (kernel_aux->hit[i] == 1) {
        u64 memory_op_id = _addresses_map.at(kernel_aux->start_end[i].start);
        kernel_memory_usage += _current_memories.at(memory_op_id)->len;
      }
    }

    if (_optimal_memory_peak < kernel_memory_usage) {
      _memory_peak_kernel = kernel_op_id;
      _optimal_memory_peak = kernel_memory_usage;
    }
  } else {
    u64 kernel_submemory_usage = 0;
    for (int i = 0; i < kernel_aux->size; i++) {
      if (kernel_aux->hit[i] == 1) {
        u64 sub_memory_op_id = _sub_addresses_map.at(kernel_aux->start_end[i].start);
        kernel_submemory_usage += _current_submemories.at(sub_memory_op_id)->len;
      }
    }

    if (_optimal_submemory_peak < kernel_submemory_usage) {
      _submemory_peak_kernel = kernel_op_id;
      _optimal_submemory_peak = kernel_submemory_usage;
    }
  }
}

void TorchMonitor::analysis_end(u32 cpu_thread, i32 kernel_id) {}

void TorchMonitor::block_enter(const ThreadId &thread_id) {
  // No operation
}

void TorchMonitor::block_exit(const ThreadId &thread_id) {
  // No operation
}

void TorchMonitor::unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id,
                               const AccessKind &access_kind, const Memory &memory, u64 pc,
                               u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
}

void TorchMonitor::flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback) {}

void TorchMonitor::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback) {
  std::ofstream output(output_dir + "memory_info.txt");
  output << "GPU memory peak: " << _memory_peak << " B" << std::endl;
  output << "Optimal GPU memory peak: " << _optimal_memory_peak << " B" << std::endl;
  output << "Peak kernel op id: " << _memory_peak_kernel << std::endl;
  output << "Number of cudaMallocs: " << _nums_cudamalloc << std::endl;
  output << "Number of cudaFrees: " << _nums_cudafree << std::endl;
  output << std::endl;

  output << "Submemory peak: " << _submemory_peak << " B" << std::endl;
  output << "Optimal submemory peak: " << _optimal_submemory_peak << " B" << std::endl;
  output << "Peak kernel op id: " << _submemory_peak_kernel << std::endl;

  output.close();
}

}  // namespace redshow
