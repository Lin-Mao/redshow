/**
 * @file memory_liveness.h
 * @author @Lin-Mao
 * @brief Split the liveness part from memory profile mode. Faster to get the liveness.
 * @version 0.1
 * @date 2022-03-11
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifndef REDSHOW_ANALYSIS_MEMORY_LIVENESS_H
#define REDSHOW_ANALYSIS_MEMORY_LIVENESS_H

#include "analysis.h"
#include "operation/operation.h"
#include "operation/kernel.h"
#include "operation/memcpy.h"
#include "operation/memfree.h"
#include "operation/memset.h"
// #include "operation/memory.h" #included in memfree

#include <fstream>

#define GPU_ANALYSIS

namespace redshow {

class MemoryLiveness final : public Analysis {

/********************************************************************
 *                  public area for redshow.cpp
 * ******************************************************************/
public:
  MemoryLiveness() : Analysis(REDSHOW_ANALYSIS_MEMORY_LIVENESS) {}

  virtual ~MemoryLiveness() = default;

  // Coarse-grained
  virtual void op_callback(OperationPtr operation, bool is_submemory = false);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u64 host_op_id, u32 cubin_id, u32 mod_id,
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
	// override the definition in base class
	Map<u32, Map<u64, std::shared_ptr<Trace>>> _kernel_trace;


  struct MemoryLivenessTrace final : public Trace {
    // only need to know memory access, don't care read or write
    // here use memory range to loge access range but not allocation and sub-allocation

    // u64: Memory:Operation->op_id
		// @Lin-Mao: don't care about read or write in this mode, just need to know access or not
		Map<u64, bool> access_memory; // map with sort but vector not
    // Map<u64, Set<MemoryRange>> read_memory;
    // Map<u64, Set<MemoryRange>> write_memory;

    MemoryLivenessTrace() = default;

    virtual ~MemoryLivenessTrace() {}
  };

  std::shared_ptr<MemoryLivenessTrace> _trace;

  // <ctx_id, Vector<op_id> log the op_id order in the same context
  Map<i32, Vector<u64>> _ctx_table;

		// <op_id, ctx_id>
	Map<u64, i32> _op_node;
  Map<u64, i32> _kernel_op_node;

  enum memory_operation {ALLOC, SET, COPYT, COPYF, ACCESS, FREE};

  // log ctx for callpath
  Map<i32, memory_operation> _ctx_node;

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

  // used to log and sort memory size
  Vector<MemoryEntry> _memory_size_list;

	// <op_id, memory>   log all allocated memory
	Map<u64, std::shared_ptr<Memory>> _memories;

	// <op_id, memory> log current memory
	Map<u64, std::shared_ptr<Memory>> _current_memories;

	// <start_addr, memory_op_id>
	Map<u64, u64> _addresses_map;

	Map<u64, Map<u64, memory_operation>> _operations;

  // current memory peak and optimal memory peak
  u64 _current_memory_usage = 0;  // to update _current_memory_peak
  u64 _current_memory_peak = 0;
  u64 _optimal_memory_peak = 0;

  struct memory_size
  {
    std::string op;
    u64 size;

    memory_size() = default;

    memory_size(std::string str, u64 s) : op(str), size(s) {};

    virtual ~memory_size(){};
  };

  Map<u64, memory_size> _memory_size_log;

  u64 _memory_peak_kernel = 0;
  

/**
 * @brief Kernel end callback
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

/**
 * @brief Register operations to _operations
 * 
 * @param memory_op_id 
 * @param op_id 
 * @param mem_op 
 */
void memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation mem_op);

/**
 * @brief Output memory opreation list
 * 
 * @param 
 */
void output_memory_operation_list(std::string file_name);

/**
 * @brief memory list with descending order
 * 
 * @param file_name 
 */
void output_memory_size_list(std::string file_name);

/**
 * @brief output kernel instances
 * 
 * @param file_name 
 */
void output_kernel_list(std::string file_name);

/**
 * @brief output memory operation sequence
 * 
 * @param file_name
 */
void output_op_sequence(std::string file_name);

/**
 * @brief output _ctx_node
 * 
 * @param file_name 
 */
void output_ctx_node(std::string file_name);

/**
 * @brief update _op_node;
 * 
 * @param op_id 
 * @param ctx_id 
 */
void update_op_node(u64 op_id, i32 ctx_id);

/**
 * @brief update _ctx_node
 * 
 * @param ctx_id 
 * @param op 
 */
void update_ctx_node(i32 ctx_id, memory_operation op);

/**
 * @brief update the ctx_id--op_id table
 * 
 * @param op_id 
 * @param ctx_id 
 */
void update_ctx_table(u64 op_id, i32 ctx_id);

/**
 * @brief for gpu liveness analysis
 * @param aux aux buffer
 */
void update_aux_hit(void* aux, u64 kernel_op_id);


};	// MemoryLiveness

}   // namespace redshow

#endif  // REDSHOW_ANALYSIS_MEMORY_LIVENESS_H

