#ifndef _RED_SHOW_H_
#define _RED_SHOW_H_

#include <gpu_patch.h>

typedef enum {
  REDSHOW_REUSE_END = 0
  REDSHOW_REUSE_DISTANCE = 1,
} redshow_analysis_type_t;

typedef enum {
  REDSHOW_SUCCESS = 0,
  REDSHOW_ERROR_NOT_IMPL = 1
} redshow_result_t;

typedef struct {
  uint32_t dummy;
} redshow_reuse_distance_t;

typedef struct {
  redshow_analysis_type_t type;
  union {
    redshow_reuse_distance_t reuse_distance;
  };
} redshow_record_data_t 

/*
 * This function is used to setup specific analysis types.
 * If multiple analysis types are enabled, redshow_record_data_get returns a sequence of record_data.
 */
redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type);

/*
 * This function is used to cancel specific analysis types.
 */
redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type);

/*
 * This function is used to register a cubin module.
 * redshow analyzes a cubin module to extract CFGs and instruction statistics.
 */
redshow_result_t redshow_cubin_register(uint32_t module_id, const char *cubin, const char *path);

/*
 * This function is used to unregister a module.
 */
redshow_result_t redshow_cubin_unregister(uint32_t module_id);

/*
 * This function is used to register a global memory region.
 */
redshow_result_t redshow_memory_register(uint64_t start, uint64_t end, const char *name);

/*
 * This function is used to unregister a global memory region.
 */
redshow_result_t redshow_memory_unregister(uint64_t start, uint64_t end);

/*
 * Let a user handles data when a trace log is done analyzing
 */
typedef void (*redshow_callback_func(uint32_t module_id, uint32_t kernel_id, redshow_record_data_t *record_data));

redshow_result_t redshow_log_data_callback_register(redshow_callback_func *func);

/*
 * Apply registered analysis to a gpu trace, analysis results are buffered.
 * redshow_callback_func is called when the analysis is done.
 * Multi-threading is enable by `export OMP_NUM_THREADS=N.`
 *
 * trace_data:
 * GPU memory trace for a single kernel launch.
 *
 * kernel_offset:
 * instruction_pc - kernel_offset = instruction_pc in the cubin
 */
redshow_result_t redshow_analyze(uint32_t module_id, uint32_t kernel_id, uint64_t kernel_offset, gpu_patch_buffer_t *trace_data);

/*
 * Get previous analysis results.
 * redshow_callback_func is called when the analysis is done.
 * record_data is a sequence of analysis results end with REDSHOW_REUSE_END.
 */
redshow_result_t redshow_record_data_get(uint32_t kernel_id, redshow_record_data_t *record_data);

/*
 * Delete previous analysis results to save memory.
 */
redshow_result_t redshow_record_data_delete(uint32_t kernel_id);

#endif  // _RED_SHOW_H_
