/**
 * @file memory_heatmap.h
 * @author @Lin-Mao
 * @brief Header file for memory_heatmap.cpp
 * @version 0.1
 * @date 2021-01-18
 * 
 * @condition:
 * sanitizer_gpu_patch_type = GPU_PATCH_TYPE_ADDRESS_PATCH
 * sanitizer_gpu_analysis_type = GPU_PATCH_TYPE_ADDRESS_ANALYSIS
 * sanitizer_gpu_analysis_blocks = 1
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef REDSHOW_ANALYSIS_MEMORY_HEATMAP_H
#define REDSHOW_ANALYSIS_MEMORY_HEATMAP_H

#include "analysis.h"
#include "operation/operation.h"
#include "operation/kernel.h"
#include "operation/memfree.h"

#include <fstream>


namespace redshow {

class MemoryHeatmap final : public Analysis {

/********************************************************************
 *                  public area for redshow.cpp
 * ******************************************************************/

  public:
  MemoryHeatmap() : Analysis(REDSHOW_ANALYSIS_MEMORY_HEATMAP) {}

  virtual ~MemoryHeatmap() = default;

  // Coarse-grained
  virtual void op_callback(OperationPtr operation, bool is_submemory = false);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 stream_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type, void* aux = NULL);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, u64 host_op_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);



/***********************************************************************
 *                 private elements area
 * ********************************************************************/

  private:

// <op_id, ctx_id>
Map<u64, i32> _op_node;

// <op_id, memory>   used to log all allocated memory
Map<u64, std::shared_ptr<Memory>> _memories;

// <op_id, memory>  used to log all allocated sub_memory
Map<u64, std::shared_ptr<Memory>> _sub_memories;


/**
 * @brief 
 * 
 */
struct HeatMapMemory {
  size_t size;
  int *array;

  HeatMapMemory() = default;

  HeatMapMemory(size_t len) : size(len) {}

};

/**
 * @brief <op_id, HeatMapMemory> to log hit frequency
 * 
 */
Map<u64, HeatMapMemory> _heatmap_list;

u64 _total_access = 0;

// functions
private:

/**
 * @brief Memory register callback function
 * 
 * @param op 
 */
void memory_op_callback(std::shared_ptr<Memory> op, bool is_submemory = false);

/**
 * @brief Update the op_id table.
 * 
 * @param op_id 
 * @param ctx_id 
 */
void update_op_node(u64 op_id, i32 ctx_id);


/**
 * @brief Update heatmap list
 * 
 */
void update_heatmap_list(u64 op_id, MemoryRange memory_range, uint32_t unit_size);




}; // class MEMORY_HEATMAP

} // namespace redshow

#endif // REDSHOW_ANALYSIS_MEMORY_HEATMAP_H 