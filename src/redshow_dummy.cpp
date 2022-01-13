#include <redshow_dummy.h>

redshow_result_t redshow_output_dir_config_dummy(redshow_analysis_type_t analysis,
                                                         const char *dir){};
redshow_result_t redshow_data_type_config_dummy(redshow_data_type_t data_type){};
redshow_result_t redshow_data_type_get_dummy(redshow_data_type_t *data_type){};
redshow_result_t redshow_pc_views_get_dummy(uint32_t *views){};
redshow_result_t redshow_mem_views_get_dummy(uint32_t *views){};
redshow_result_t redshow_approx_level_config_dummy(redshow_approx_level_t level){};
redshow_result_t redshow_approx_get_dummy(int *degree_f32, int *degree_f64){};
redshow_result_t redshow_analysis_enable_dummy(redshow_analysis_type_t analysis_type){};
redshow_result_t redshow_analysis_enabled_dummy(redshow_analysis_type_t analysis_type){};
redshow_result_t redshow_analysis_disable_dummy(redshow_analysis_type_t analysis_type){};
redshow_result_t redshow_analysis_config_dummy(redshow_analysis_type_t analysis_type,
                                                       redshow_analysis_config_type_t config,
                                                       bool enable){};
redshow_result_t redshow_cubin_register_dummy(uint32_t cubin_id, uint32_t mod_id,
                                                      uint32_t nsymbols, const uint64_t *symbol_pcs,
                                                      const char *path){};
redshow_result_t redshow_cubin_cache_register_dummy(uint32_t cubin_id, uint32_t mod_id,
                                                            uint32_t nsymbols, uint64_t *symbol_pcs,
                                                            const char *path){};
redshow_result_t redshow_cubin_unregister_dummy(uint32_t cubin_id, uint32_t mod_id){};
redshow_result_t redshow_memory_register_dummy(int32_t memory_id, uint64_t host_op_id,
                                                       uint64_t start, uint64_t end){};
redshow_result_t redshow_memory_unregister_dummy(uint64_t host_op_id, uint64_t start,
                                                         uint64_t end){};
redshow_result_t redshow_memory_query_dummy(uint64_t host_op_id, uint64_t start,
                                                    int32_t *memory_id, uint64_t *memory_op_id,
                                                    uint64_t *shadow_start, uint64_t *len){};
redshow_result_t redshow_memory_ranges_get_dummy(uint64_t host_op_id, uint64_t limit,
                                                         gpu_patch_analysis_address_t *start_end,
                                                         uint32_t *len){};
redshow_result_t redshow_memcpy_register_dummy(int32_t memcpy_id, uint64_t host_op_id,
                                                       bool src_host, uint64_t src_start, bool dst_host,
                                                       uint64_t dst_start, uint64_t len){};
redshow_result_t redshow_memset_register_dummy(int32_t memset_id, uint64_t host_op_id,
                                                       uint64_t start, uint32_t value, uint64_t len){};
redshow_result_t redshow_log_data_callback_register_dummy(redshow_log_data_callback_func func){};
redshow_result_t redshow_record_data_callback_register_dummy(
    redshow_record_data_callback_func func, uint32_t pc_views_limit, uint32_t mem_views_limit){};
redshow_result_t redshow_analyze_dummy(uint32_t cpu_thread, uint32_t cubin_id, uint32_t mod_id,
                                               int32_t kernel_id, uint64_t host_op_id,
                                               gpu_patch_buffer_t *trace_data){};
redshow_result_t redshow_tool_dtoh_register_dummy(redshow_tool_dtoh_func func){};
redshow_result_t redshow_kernel_begin_dummy(uint32_t cpu_thread, int32_t kernel_id,
                                                    uint64_t host_op_id){};
redshow_result_t redshow_kernel_end_dummy(uint32_t cpu_thread, int32_t kernel_id,
                                                  uint64_t host_op_id){};
redshow_result_t redshow_analysis_begin_dummy(){};
redshow_result_t redshow_analysis_end_dummy(){};
redshow_result_t redshow_flush_thread_dummy(uint32_t cpu_thread){};
redshow_result_t redshow_flush_dummy(){};
redshow_result_t redshow_flush_now_dummy(uint32_t cpu_thread){};
