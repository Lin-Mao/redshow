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

// Enable liveness analysis on GPU (HPCRUN_SANITIZER_LIVENESS_ONGPU=1)
#define REDSHOW_GPU_ANALYSIS
// Enable PyTorch submemory analysis (HPCRUN_SANITIZER_TORCH_ANALYSIS=1)
#define REDSHOW_TORCH_SUBMEMORY_ANALYSIS

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

/********************************************************************************
 *********************************  VARIABLES  **********************************
 *******************************************************************************/

	// override the definition in base class
	Map<u32, Map<u64, std::shared_ptr<Trace>>> _kernel_trace;


  struct MemoryLivenessTrace final : public Trace {
    // only need to know memory access, don't care read or write
    // here use memory range to loge access range but not allocation and sub-allocation

    // u64: Memory:Operation->op_id
		// @Lin-Mao: don't care about read or write in this mode, just need to know access or not
		Map<u64, bool> access_memory; // map with sort but vector not

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
    Map<u64, bool> access_submemory;
#endif

    MemoryLivenessTrace() = default;

    virtual ~MemoryLivenessTrace() {}
  };

  std::shared_ptr<MemoryLivenessTrace> _trace;

  // <ctx_id, Vector<op_id> log the op_id order in the same context
  Map<i32, Vector<u64>> _ctx_table;

		// <op_id, ctx_id>
	Map<u64, i32> _op_node;
  Map<u64, i32> _kernel_op_node;

  typedef enum memory_operation {
    REDSHOW_MEMORY_ALLOC = 0,
    REDSHOW_MEMORY_SET = 1,
    REDSHOW_MEMORY_COPYT = 2,
    REDSHOW_MEMORY_COPYF = 3,
    REDSHOW_MEMORY_ACCESS = 4,
    REDSHOW_MEMORY_FREE = 5,
    REDSHOW_SUBMEMORY_ALLOC = 6,
    REDSHOW_SUBMEMORY_FREE = 7
  } memory_operation_t;

  // log ctx for callpath
  Map<i32, memory_operation_t> _ctx_node;

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

	Map<u64, Map<u64, memory_operation_t>> _operations;

  // current memory peak and optimal memory peak
  u64 _current_memory_usage = 0;  // to update _current_memory_peak
  u64 _current_memory_peak = 0;
  u64 _optimal_memory_peak = 0;
  u64 _memory_peak_kernel = 0;

  struct memory_size
  {
    std::string op;
    u64 size;

    memory_size() = default;

    memory_size(std::string str, u64 s) : op(str), size(s) {};

    virtual ~memory_size(){};
  };

  Map<u64, memory_size> _memory_size_log;


#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
  // <op_id, submemory>   log all allocated submemory
	Map<u64, std::shared_ptr<Memory>> _submemories;

	// <op_id, submemory> log current submemory
	Map<u64, std::shared_ptr<Memory>> _current_submemories;

  // <start_addr, sub_op_id> for mapping access to sub-memories
  Map<u64, u64> _sub_addresses_map;

  // for liveness
  Map<u64, Map<u64, memory_operation_t>> _sub_operations;

  Map<u64, memory_size> _submemory_size_log;

  // get sub-alloc size list
  Vector<MemoryEntry> _submemory_size_list;

  u64 _current_submemory_usage = 0;  // to update _current_submemory_peak
  u64 _current_submemory_peak = 0;
  u64 _optimal_submemory_peak = 0;
  u64 _submemory_peak_kernel = 0;

  // For python state
  struct PythonState {
    std::string file_name;
    std::string function_name;
    size_t function_first_lineno;
    size_t lineno;

    PythonState() = default;

    PythonState(std::string file_name, std::string function_name, size_t function_first_lineno, size_t lineno) \
      : file_name(file_name), function_name(function_name), function_first_lineno(function_first_lineno), \
      lineno(lineno) {}

    bool operator==(const PythonState &other) const {
      if (this->file_name.compare(other.file_name) != 0) {
        return false;
      } else if (this->function_name.compare(other.function_name)) {
        return false;
      } else if (this->function_first_lineno != other.function_first_lineno) {
        return false;
      } else if (this->lineno != other.lineno) {
        return false;
      } else {
        return true;
      }
    }
  };

  // <op_id, vector<python_states>>
  Map<u64, Vector<PythonState>> _torch_python_states;

  // <op_id, op_type>
  Map<u64, std::string> _sub_op_node;

#endif

/********************************************************************************
 *********************************  FUNCTIONS  **********************************
 *******************************************************************************/

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
void memory_operation_register(u64 memory_op_id, u64 op_id, memory_operation_t mem_op, bool is_sub = false);

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
 * @brief output sub-memory size growth sequence
 * 
 * @param file_name
 */
void output_memory_size_growth_sequence(std::string file_name);

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
void update_ctx_node(i32 ctx_id, memory_operation_t op);

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
void update_aux_hit(void* aux, u64 kernel_op_id, bool is_sub = false);

#ifdef REDSHOW_TORCH_SUBMEMORY_ANALYSIS
/**
 * @brief output sub-memory liveness
 * 
 * @param file_name
 */
void output_submemory_liveness(std::string file_name);

/**
 * @brief output sub-memory size list
 * 
 * @param file_name
 */
void output_submemory_size_list(std::string file_name);

/**
 * @brief output submemory info
 * 
 * @param file_name
 */
void output_submemory_info(std::string file_name);

/**
 * @brief output sub-memory size growth sequence
 * 
 * @param filename
 **/
void output_submemory_size_growth_sequence(std::string filename);

/**
 * @brief output python states
 * 
 * @param file_name
 */
void output_torch_python_states(std::string file_name);

/**
 * @brief output merged pytorch states
 **/
void output_merged_torch_python_states(std::string filename);

/**
 * @brief udpate torch python states
 * 
 * @param op_id
 * 
 * @param type
 */
void update_torch_python_states(u64 op_id);

/**
 * @brief verify sub_allocation on gpu
 **/
bool verify_sub_allocation_ongpu(std::shared_ptr<Memory> sub_alloc);

#endif


};	// MemoryLiveness

}   // namespace redshow

#endif  // REDSHOW_ANALYSIS_MEMORY_LIVENESS_H

