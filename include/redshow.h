#ifndef _RED_SHOW_H_
#define _RED_SHOW_H_

#include <gpu-patch.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

typedef enum {
  REDSHOW_REUSE_END = 0,
  REDSHOW_REUSE_DISTANCE = 1
} redshow_analysis_type_t;

typedef enum {
  REDSHOW_SUCCESS = 0,
  REDSHOW_ERROR_NOT_IMPL = 1,
  REDSHOW_ERROR_NOT_EXIST_ENTRY = 2,
  REDSHOW_ERROR_DUPLICATE_ENTRY = 3,
  REDSHOW_ERROR_NOT_REGISTER_CALLBACK = 4,
  REDSHOW_ERROR_NO_SUCH_FILE = 5,
  REDSHOW_ERROR_FAILED_ANALYZE_CUBIN = 6
} redshow_result_t;

typedef struct {
  uint32_t dummy;
} redshow_reuse_distance_t;

typedef struct {
  redshow_analysis_type_t type;
  union {
    redshow_reuse_distance_t reuse_distance;
  };
} redshow_record_data_t;

/*
 * Configure the output analysis result directory
 * 
 * Thread-Safety: NO
 */
EXTERNC redshow_result_t redshow_analysis_output(const char *path);

/*
 * This function is used to setup specific analysis types.
 * 
 * Thread-Safety: NO
 */
EXTERNC redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type);

/*
 * This function is used to cancel specific analysis types.
 * 
 * Thread-Safety: NO
 */
EXTERNC redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type);

/*
 * This function is used to register a cubin module.
 * redshow analyzes a cubin module to extract CFGs and instruction statistics.
 * 
 * Thread-Safety: YES
 */
EXTERNC redshow_result_t redshow_cubin_register(uint32_t cubin_id, const char *path);

/*
 * This function is used to unregister a module.
 * 
 * Thread-Safety: YES
 */
EXTERNC redshow_result_t redshow_cubin_unregister(uint32_t cubin_id);

/*
 * This function is used to register a global memory region.
 * 
 * Thread-Safety: YES
 */
EXTERNC redshow_result_t redshow_memory_register(uint64_t start, uint64_t end, uint64_t memory_id);

/*
 * This function is used to unregister a global memory region.
 * 
 * Thread-Safety: YES
 */
EXTERNC redshow_result_t redshow_memory_unregister(uint64_t start, uint64_t end);

/*
 * Let a user handle data when a trace log is done analyzing
 *
 * Thread-Safety: NO
 */
typedef void (*redshow_log_data_callback_func)(uint64_t kernel_id, gpu_patch_buffer_t *trace_data);

EXTERNC redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func);

/*
 * Apply registered analysis to a gpu trace, analysis results are buffered.
 * redshow_callback_func is called when the analysis is done.
 * Multi-threading is enable by `export OMP_NUM_THREADS=N.`
 *
 * cubin_id:
 * Lookup correponding cubin
 *
 * kernel_id:
 * Unique identifier for a calling context
 *
 * func_index:
 * Symbol index in the cubin
 *
 * func_addr:
 * Function address in memory
 *
 * trace_data:
 * GPU memory trace for a single kernel launch.
 *
 * kernel_offset:
 * instruction_pc - kernel_offset = instruction_pc in the cubin
 * 
 * Thread-Safety: YES
 */
EXTERNC redshow_result_t redshow_analyze(uint32_t cubin_id, uint32_t func_index,
  uint64_t func_addr, uint64_t kernel_id, gpu_patch_buffer_t *trace_data);

#endif  // _RED_SHOW_H_
