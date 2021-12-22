/**
 * @file memory_profile.h
 * @author @Lin-Mao
 * @brief Header file for memory_profile.cpp
 * @version 0.1
 * @date 2021-09-28
 * 
 * @condition:
 * sanitizer_gpu_patch_type = GPU_PATCH_TYPE_ADDRESS_PATCH
 * sanitizer_gpu_analysis_type = GPU_PATCH_TYPE_ADDRESS_ANALYSIS
 * sanitizer_gpu_analysis_blocks = 1
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#ifndef REDSHOW_ANALYSIS_MEMORY_PROFILE_H
#define REDSHOW_ANALYSIS_MEMORY_PROFILE_H

#include "analysis.h"
#include "operation/operation.h"
#include "operation/memory.h"
#include "operation/kernel.h"

#include <fstream>


namespace redshow {

class MemoryProfile final : public Analysis {

/********************************************************************
 *                  public area for redshow.cpp
 * ******************************************************************/

  public:
  MemoryProfile() : Analysis(REDSHOW_ANALYSIS_VALUE_PATTERN) {}

  // Coarse-grained
  virtual void op_callback(OperationPtr operation, bool is_submemory = false);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  // Flush
  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

  ~MemoryProfile() {}


/***********************************************************************
 *                 private elements area
 * ********************************************************************/

  private:

  struct MemoryProfileTrace final : public Trace {
    // only need to know memory access, don't care read or write
    // here use memory range to loge access range but not allocation and sub-allocation

    // u64: Memory:Operation->op_id
    Map<u64, Set<MemoryRange>> read_memory;
    Map<u64, Set<MemoryRange>> write_memory;

    MemoryProfileTrace() = default;

    virtual ~MemoryProfileTrace() {}
  };

  std::shared_ptr<MemoryProfileTrace> _trace;

// <op_id, ctx_id>
Map<u64, i32> _op_node;

// <op_id, memory>   used to log all allocated memory
Map<u64, std::shared_ptr<Memory>> _memories;

// <op_id, memory>  used to log all allocated sub_memory
Map<u64, std::shared_ptr<Memory>> _sub_memories;


/**
 * @brief <kernel_op_id, <memory_op_id, set<memory_range>>.  
 * Define this Map to store all the blank chunks of all objects for all kernel. 
 */
Map<i32, Map<u64, Set<MemoryRange>>> _blank_chunks;


/**
 * @brief Map<memory_op_id, kernel_id> accessed_op_id which is processed in _blank_chunks
 * 
 */
Map<u64, i32> _accessed_memories;

/**
 * @brief Map<memory_op_id, largest_chunk>
 */
Map<u64, size_t> larget_chunk_with_memories;


struct ChunkFragmentation {
  size_t largest_chunk;
  float fragmentation;

  ChunkFragmentation() = default;

  ChunkFragmentation(size_t chunk, float frag) : largest_chunk(chunk), fragmentation(frag) {}
};

/**
 * @brief <thread_id, <kernel_op_id, <memory_op_id, fragmentation>>>>
 * This Map is to log the fragmentation under which cpu_thread, in which kernel, at what op time, of which memory object.
 */
Map<u32, Map<u64, Map<u64, ChunkFragmentation>>> _object_fragmentation_of_kernel_per_thread;




// functions
private:

/**
 * @brief To merge access memory range in unit_access.
 * 
 * @param memory 
 * @param memory_range 
 */
void merge_memory_range(Set<MemoryRange> &memory, const MemoryRange &memory_range);


/**
 * @brief Kernel callback function
 * 
 * @param op 
 */
void kernel_op_callback(std::shared_ptr<Kernel> op);

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
 * @brief Update the '_blank_chunks' which is to store the unused memory range in every object
 * 
 * @param op_id 
 * @param kernel_id 
 * @param range_iter 
 */
void update_blank_chunks(i32 kernel_id, u64 memory_op_id, MemoryRange range_iter);


/**
 * @brief Update the framentation map. Called after each kernel was finished.
 * 
 * @param cpu_thread 
 * @param kernel_id 
 * @param op_id 
 */
void update_object_fragmentation_in_kernel(u32 cpu_thread, i32 kernel_id);




}; // class MemoryProfile

} // namespace redshow

#endif // REDSHOW_ANALYSIS_MEMORY_PROFILE_H 