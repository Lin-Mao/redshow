#ifndef REDSHOW_H
#define REDSHOW_H

#include <gpu-patch.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

typedef enum redshow_analysis_type {
  REDSHOW_ANALYSIS_UNKNOWN = 0,
  REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY = 1,
  REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY = 2,
  REDSHOW_ANALYSIS_VALUE_PATTERN = 3,
  REDSHOW_ANALYSIS_DATA_FLOW = 4
} redshow_analysis_type_t;

typedef enum redshow_access_type {
  REDSHOW_ACCESS_UNKNOWN = 0,
  REDSHOW_ACCESS_READ = 1,
  REDSHOW_ACCESS_WRITE = 2
} redshow_access_type_t;

typedef enum redshow_data_type {
  REDSHOW_DATA_UNKNOWN = 0,
  REDSHOW_DATA_FLOAT = 1,
  REDSHOW_DATA_INT = 2
} redshow_data_type_t;

typedef enum redshow_memory_type {
  REDSHOW_MEMORY_UNKNOWN = 0,
  REDSHOW_MEMORY_SHARED = 1,
  REDSHOW_MEMORY_LOCAL = 2,
  REDSHOW_MEMORY_GLOBAL = 3,
  REDSHOW_MEMORY_CONSTANT = 4,
  REDSHOW_MEMORY_UVM = 5,
  REDSHOW_MEMORY_HOST = 6
} redshow_memory_type_t;

typedef enum redshow_result {
  REDSHOW_SUCCESS = 0,
  REDSHOW_ERROR_NOT_IMPL = 1,
  REDSHOW_ERROR_NOT_EXIST_ENTRY = 2,
  REDSHOW_ERROR_DUPLICATE_ENTRY = 3,
  REDSHOW_ERROR_NOT_REGISTER_CALLBACK = 4,
  REDSHOW_ERROR_NO_SUCH_FILE = 5,
  REDSHOW_ERROR_FAILED_ANALYZE_CUBIN = 6,
  REDSHOW_ERROR_FAILED_ANALYZE_TRACE = 7,
  REDSHOW_ERROR_NO_SUCH_APPROX = 8,
  REDSHOW_ERROR_NO_SUCH_DATA_TYPE = 9,
  REDSHOW_ERROR_NO_SUCH_ANALYSIS = 10
} redshow_result_t;

typedef enum redshow_approx_level {
  REDSHOW_APPROX_NONE = 0,
  REDSHOW_APPROX_MIN = 1,
  REDSHOW_APPROX_LOW = 2,
  REDSHOW_APPROX_MID = 3,
  REDSHOW_APPROX_HIGH = 4,
  REDSHOW_APPROX_MAX = 5,
} redshow_approx_level_t;

typedef struct redshow_record_view {
  uint32_t function_index;
  uint64_t pc_offset;
  int32_t memory_id;
  uint64_t memory_op_id;
  uint64_t red_count;
  uint64_t access_count;
} redshow_record_view_t;

typedef struct redshow_record_data {
  uint32_t num_views;
  redshow_analysis_type_t analysis_type;
  redshow_access_type_t access_type;
  redshow_record_view_t *views;
} redshow_record_data_t;

/**
 * @brief Config default output directory
 *
 * @param dir
 * @return EXTERNC
 *
 * @thread-safe: No
 */
EXTERNC redshow_result_t redshow_output_dir_config(redshow_analysis_type_t analysis,
                                                   const char *dir);

/**
 * @brief Config default data type
 *
 * @param data_type
 * @return redshow_result_t
 *
 * @thread-safe: No
 */
EXTERNC redshow_result_t redshow_data_type_config(redshow_data_type_t data_type);

/**
 * @brief Get default data type
 *
 * @param data_type
 * @return redshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_data_type_get(redshow_data_type_t *data_type);

/**
 * @brief Get pc views limit
 *
 * @param views
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_pc_views_get(uint32_t *views);

/**
 * @brief Get mem views limit
 *
 * @param views
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_mem_views_get(uint32_t *views);

/**
 * @brief Config floating point redundancy approximate level
 *
 * @param level
 * @return reshow_result_t
 *
 * @thread-safe: No
 */
EXTERNC redshow_result_t redshow_approx_level_config(redshow_approx_level_t level);

/**
 * @brief
 *
 * @param degree_f32
 * @param degree_f64
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_approx_get(int *degree_f32, int *degree_f64);

/**
 * @brief This function is used to setup specific analysis types.
 *
 * @param analysis_type
 * @return reshow_result_t
 *
 * @thread-safe: No
 */
EXTERNC redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type);

/**
 * @brief This function is used to cancel specific analysis types.
 *
 * @param analysis_type
 * @return reshow_result_t
 *
 * @thread-safe: NO
 */
EXTERNC redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type);

/**
 * @brief This function is used to register a cubin module. redshow analyzes a cubin module to
 * extract CFGs and instruction statistics.
 *
 * @param cubin_id Unique identifier for cubins
 * @param mod_id Unique identifier for modules that use the cubin
 * @param nsymbols Number of symbols in cubin
 * @param symbol_pcs An array of symbol start addresses in memory. Use 0 for non-function symbols
 * @param path
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_cubin_register(uint32_t cubin_id, uint32_t mod_id,
                                                uint32_t nsymbols, const uint64_t *symbol_pcs,
                                                const char *path);

/**
 * @brief For a large-scale program that loads a large number of CUBINs, we do not analyze every of
 * them because not all the cubins will be used. Instead, we cache the cubin's symbols and path and
 * analyze the cubins we use
 *
 * @param cubin_id Unique identifier for cubins
 * @param mod_id Unique identifier for modules that use the cubin
 * @param nsymbols Number of symbols in cubin
 * @param symbol_pcs An array of symbol start addresses in memory. Use 0 for non-function symbols
 * @param path
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_cubin_cache_register(uint32_t cubin_id, uint32_t mod_id,
                                                      uint32_t nsymbols, uint64_t *symbol_pcs,
                                                      const char *path);

/**
 * @brief This function is used to unregister a module.
 *
 * @param cubin_id Unique identifier for cubins
 * @param mod_id Unique identifier for modules that use the cubin
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_cubin_unregister(uint32_t cubin_id, uint32_t mod_id);

/**
 * @brief This function is used to register a global memory region.
 *
 * @param memory_id
 * @param host_op_id
 * @param start
 * @param end
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_memory_register(int32_t memory_id, uint64_t host_op_id,
                                                 uint64_t start, uint64_t end);

/**
 * @brief This function is used to unregister a global memory region.
 *
 * @param start
 * @param end
 * @param host_op_id
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_memory_unregister(uint64_t host_op_id, uint64_t start,
                                                   uint64_t end);

/**
 * @brief This funciton is used to query the address of a shadow memory
 *
 * @param host_op_id Unique identifier of the current timestamp
 * @param start The address of the memory object
 * @param memory_id The calling context of the memory object
 * @param memory_op_id The operation id the memory object
 * @param shadow_start The shadow memory address of the memory object
 * @param len The size of the memory object
 * @return reshow_result_t
 *
 * @thread-safe YES
 */
EXTERNC redshow_result_t redshow_memory_query(uint64_t host_op_id, uint64_t start,
                                              int32_t *memory_id, uint64_t *memory_op_id,
                                              uint64_t *shadow_start, uint64_t *len);

/**
 * @brief This funciton is used to track a memcpy operation
 *
 * @param memcpy_id Calling context of the mempry operation
 * @param host_op_id Unique identifier of a memcpy operation
 * @param len Number of copied bytes
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_memcpy_register(int32_t memcpy_id, uint64_t host_op_id,
                                                 bool src_host, uint64_t src_start, bool dst_host,
                                                 uint64_t dst_start, uint64_t len);

/**
 * @brief This funciton is used to track a memset operation
 *
 * @param memset_id
 * @param host_op_id
 * @param start
 * @param value
 * @param len
 * @return reshow_result_t
 *
 * @thread-safe: YES
 */
EXTERNC redshow_result_t redshow_memset_register(int32_t memset_id, uint64_t host_op_id,
                                                 uint64_t start, uint32_t value, uint64_t len);

/**
 * @brief Callback function prototype
 *
 * @thread-safe NO
 */
typedef void (*redshow_log_data_callback_func)(int32_t kernel_id, gpu_patch_buffer_t *trace_data);

/**
 * @brief Let a user handle data when a trace log is done analyzing
 *
 * @param func
 * @return reshow_result_t
 *
 * @thread-safe NO
 */
EXTERNC redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func);

/**
 * @brief Callback function prototype
 *
 */
typedef void (*redshow_record_data_callback_func)(uint32_t cubin_id, int32_t kernel_id,
                                                  redshow_record_data_t *record_data);

/**
 * @brief Let a user get overview data for all kernels when the program is finished.
 *
 * @param func
 * @param pc_views_limit
 * @param mem_views_limit
 * @return reshow_result_t
 */
EXTERNC redshow_result_t redshow_record_data_callback_register(
    redshow_record_data_callback_func func, uint32_t pc_views_limit, uint32_t mem_views_limit);

/**
 * @brief Apply registered analysis to a gpu trace, analysis results are buffered.
 * redshow_callback_func is called when the analysis is done.
 * Multi-threading is enable by `export OMP_NUM_THREADS=N.`
 *
 * First use binary search to find an enclosed region of function addresses
 * instruction_offset = instruction_pc - function_address
 *
 * @param cpu_thread Which thread launches the kernel
 * @param cubin_id Lookup correponding cubin
 * @param mod_id Unique identifier for modules that use the cubin
 * @param kernel_id Unique identifier for a calling context
 * @param host_op_id Unique identifier for the operation
 * @param trace_data GPU memory trace for a single kernel launch.
 * @return reshow_result_t
 *
 * @thread-safe YES
 */
EXTERNC redshow_result_t redshow_analyze(uint32_t cpu_thread, uint32_t cubin_id, uint32_t mod_id,
                                         int32_t kernel_id, uint64_t host_op_id,
                                         gpu_patch_buffer_t *trace_data);

/**
 * @brief Callback function prototype
 *
 */
typedef void (*redshow_tool_dtoh_func)(uint64_t host_start, uint64_t device_start, uint64_t len);

/**
 * @brief Register dtoh function
 *
 * @param func
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_tool_dtoh_register(redshow_tool_dtoh_func func);

/**
 * @brief when a kernel starts
 *
 * @param cpu_thread
 * @param kernel_id
 * @param host_op_id
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_kernel_begin(uint32_t cpu_thread, int32_t kernel_id,
                                              uint64_t host_op_id);

/**
 * @brief when a kernel ends
 *
 * @param cpu_thread
 * @param kernel_id
 * @param host_op_id
 * @return EXTERNC
 */
EXTERNC redshow_result_t redshow_kernel_end(uint32_t cpu_thread, int32_t kernel_id,
                                            uint64_t host_op_id);

/**
 * @brief Mark the begin of the current analysis region
 *
 * @return reshow_result_t
 *
 * @thread-safe YES
 */
EXTERNC redshow_result_t redshow_analysis_begin();

/**
 * @brief Mark the end of the current analysis region
 *
 * @return reshow_result_t
 *
 * @thread-safe YES
 */
EXTERNC redshow_result_t redshow_analysis_end();

/**
 * @brief Flush back all the result. This function is supposed to be called when all the analysis
 * and kernel launches of each thread is done.
 *
 * @param cpu_thread
 * @return reshow_result_t
 *
 * @thread-safe YES
 */
EXTERNC redshow_result_t redshow_flush_thread(uint32_t cpu_thread);

/**
 * @brief  Flush back all the result. This function is supposed to be called when all the analysis
 * and kernel launches of the whole program is done.
 *
 * @return reshow_result_t
 *
 * @thread-safe NO
 */
EXTERNC redshow_result_t redshow_flush();

#endif  // REDSHOW_H
