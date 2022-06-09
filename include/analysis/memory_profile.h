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
// #include "operation/memory.h" // included in operation/memfree.h
#include "operation/kernel.h"
#include "operation/memfree.h"
#include "operation/memcpy.h"
#include "operation/memset.h"

#include <fstream>


namespace redshow {

class MemoryProfile final : public Analysis {

/********************************************************************
 *                  public area for redshow.cpp
 * ******************************************************************/

  public:
  MemoryProfile() : Analysis(REDSHOW_ANALYSIS_MEMORY_PROFILE) {}

  virtual ~MemoryProfile() = default;

  // Coarse-grained
  virtual void op_callback(OperationPtr operation, bool is_submemory = false);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id,
                              GPUPatchType type);

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
  // redefine 
  Map<u32, Map<u64, std::shared_ptr<Trace>>> _kernel_trace;

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

// <op_id, ctx_id> to store objects and kernels
Map<u64, i32> _op_node;

// memory entry
struct MemoryEntry {
  u64 op_id;
  size_t size;

  MemoryEntry() = default;
  MemoryEntry(u64 op_id, size_t size) : op_id(op_id), size(size) {}

  bool operator<(const MemoryEntry &other) const { return this->size < other.size; }

  bool operator>(const MemoryEntry &other) const { return this->size > other.size; }
  
  virtual ~MemoryEntry() {}
};

// <op_id, memory>   used to log all allocated memory
Map<u64, std::shared_ptr<Memory>> _memories;
Map<u64, std::shared_ptr<Memory>> _current_memories;
Vector<MemoryEntry> _memory_size_list;

// <address, memory_op_id> used to map cudaFree later
std::unordered_map<u64, u64> _addresses_map;

// <op_id, memory>  used to log all allocated sub_memory
Map<u64, std::shared_ptr<Memory>> _sub_memories;

// <cudaMalloc, cudaFree>, used for liveness
Map<u64, u64> _liveness_map;

enum memory_operation {ALLOC, SET, COPYT, COPYF, ACCESS, FREE};

// <cudaMallo, Vector<cudaMemset, cudaMemcpy, cudaFree>>
Map<u64, Map<u64, memory_operation>> _operations;

// first alloc and last free
u64 _first_alloc_op_id = 9223372039002259456u;  // 2^63 + 2^31
u64 _last_free_op_id = 0;

// <op_id, size> in kernel launch
Map<u64, size_t> _kernel_memory_size;

/**
 * @brief <kernel_op_id, <memory_op_id, set<memory_range>>.  
 * Define this Map to store all the blank chunks of all objects for all kernel. 
 */
Map<u64, Map<u64, Set<MemoryRange>>> _blank_chunks;


/**
 * @brief Map<memory_op_id, kernel_op_id> accessed_op_id which is processed in _blank_chunks
 *  can be opted out
 */
Map<u64, u64> _accessed_memories;

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
 * @brief Memory unregister callback function
 * 
 */
void memfree_op_callback(std::shared_ptr<Memfree> op, bool is_submemory = false);

/**
 * @brief Memcpy register callback function
 * 
 * @param op 
 */
void memcpy_op_callback(std::shared_ptr<Memcpy> op);

/**
 * @brief Memset register callback function
 * 
 * @param op 
 */
void memset_op_callback(std::shared_ptr<Memset> op);

void memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op);

/**
 * @brief Update the op_id map
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
void update_blank_chunks(u64 kernel_op_id, u64 memory_op_id, MemoryRange range_iter);


/**
 * @brief Update the framentation map. Called after each kernel was finished.
 * 
 * @param cpu_thread 
 * @param kernel_id 
 * @param op_id 
 */
void update_object_fragmentation_in_kernel(u32 cpu_thread, u64 kernel_op_id);


/**
 * @brief Output largest list of memories
 * 
 * @param 
 */
void output_memory_size_list(std::string file_name);

/**
 * @brief Output memory opreation list
 * 
 * @param 
 */
void output_memory_operation_list(std::string file_name);




}; // class MemoryProfile

} // namespace redshow

#endif // REDSHOW_ANALYSIS_MEMORY_PROFILE_H 