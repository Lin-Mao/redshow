#include <redshow.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "analysis/memory_profile.h"
#include "analysis/data_flow.h"
#include "analysis/spatial_redundancy.h"
#include "analysis/temporal_redundancy.h"
#include "analysis/value_pattern.h"
#include "binutils/cubin.h"
#include "binutils/instruction.h"
#include "binutils/real_pc.h"
#include "binutils/symbol.h"
#include "common/map.h"
#include "common/set.h"
#include "common/utils.h"
#include "common/vector.h"
#include "operation/kernel.h"
#include "operation/memcpy.h"
#include "operation/memory.h"
#include "operation/memset.h"
#include "operation/memfree.h"

#ifdef DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

using namespace redshow;

/*
 * Global data structures
 */

static LockableMap<uint32_t, Cubin> cubin_map;

static LockableMap<uint32_t, CubinCache> cubin_cache_map;


// MemoryMap(<range, object>) is a map with current object
typedef Map<MemoryRange, std::shared_ptr<Memory>> MemoryMap;
// <op_id, memory_map>
static LockableMap<uint64_t, MemoryMap> memory_snapshot;

// reuse memory structure for sub-allocation
static LockableMap<uint64_t, MemoryMap> sub_memory_snapshot;

// Init analysis instance
// TODO(Keren): Separate address and full analysis modes
static Map<redshow_analysis_type_t, std::shared_ptr<Analysis>> analysis_enabled;

static Map<redshow_analysis_type_t, std::string> output_dir;

static redshow_log_data_callback_func log_data_callback = NULL;

static redshow_record_data_callback_func record_data_callback = NULL;

static thread_local uint64_t mini_host_op_id = 0;

static uint32_t pc_views_limit = PC_VIEWS_LIMIT;
static uint32_t mem_views_limit = MEM_VIEWS_LIMIT;

static int decimal_degree_f32 = VALID_FLOAT_DIGITS;
static int decimal_degree_f64 = VALID_DOUBLE_DIGITS;

static redshow_data_type_t default_data_type = REDSHOW_DATA_UNKNOWN;

static redshow_result_t analyze_cubin(const char *path, SymbolVector &symbols,
                                      InstructionGraph &inst_graph) {
  redshow_result_t result = REDSHOW_SUCCESS;

  std::string cubin_path = std::string(path);
  auto iter = cubin_path.rfind("/");
  if (iter == std::string::npos) {
    result = REDSHOW_ERROR_NO_SUCH_FILE;
  } else {
    // x/x.cubin
    // 012345678
    std::string cubin_name = cubin_path.substr(iter + 1, cubin_path.size() - iter);
    // x/cubins/x.cubin
    iter = cubin_path.rfind("/", iter - 1);
    std::string dir_name = cubin_path.substr(0, iter);
    std::string inst_path = dir_name + "/structs/nvidia/" + cubin_name + ".inst";

    // Prevent boost from core dump
    std::ifstream f(inst_path.c_str());
    if (f.good() == false) {
      result = REDSHOW_ERROR_NO_SUCH_FILE;
    } else {
      // instructions are analyzed before hpcrun
      if (InstructionParser::parse(inst_path, symbols, inst_graph)) {
        result = REDSHOW_SUCCESS;
      } else {
        result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
      }
    }
  }

  return result;
}

static redshow_result_t trace_analyze_address_patch(int32_t kernel_id, MemoryMap *memory_map,
                                                    gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  size_t size = trace_data->head_index;
  gpu_patch_record_address_t *records =
      reinterpret_cast<gpu_patch_record_address_t *>(trace_data->records);

  // Dummy entries
  AccessKind access_kind;
  ThreadId thread_id{0, 0};

  for (size_t i = 0; i < size; ++i) {
    // Iterate over each record
    gpu_patch_record_address_t *record = records + i;

    for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
      if ((record->active & (0x1u << j)) == 0) {
        continue;
      }

      // <start, end>
      MemoryRange memory_range(record->address[j], record->address[j] + record->size);

      auto iter = memory_map->prev(memory_range);
      uint64_t memory_op_id = 0;
      int32_t memory_id = 0;
      uint64_t memory_size = 0;
      uint64_t memory_addr = 0;
      if (iter != memory_map->end()) {
        if (record->address[j] >= iter->second->memory_range.start &&
            record->address[j] + record->size <= iter->second->memory_range.end) {
          memory_op_id = iter->second->op_id;
          memory_id = iter->second->ctx_id;
          memory_size = record->size;
          memory_addr = memory_range.start;
        } else {
          // TODO(Keren): Investigate what are the causes
          // Prevent out of bound memory accesses
          continue;
        }
      }

      if (memory_op_id == 0) {
        // Unknown memory object
        continue;
      }

      Memory memory = Memory(memory_op_id, memory_id, memory_addr, memory_size);
      // XXX(Keren): Need to separate address analysis with value analysis
      for (auto aiter : analysis_enabled) {
        aiter.second->unit_access(kernel_id, thread_id, access_kind, memory, 0, 0, 0, 0,
                                  static_cast<GPUPatchFlags>(record->flags));
      }
    }
  }
  return result;
}

static redshow_result_t trace_analyze_address_analysis(int32_t kernel_id, MemoryMap *memory_map,
                                                       gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  size_t size = trace_data->head_index;
  gpu_patch_analysis_address_t *records =
      reinterpret_cast<gpu_patch_analysis_address_t *>(trace_data->records);

  // Dummy entries
  AccessKind access_kind;
  ThreadId thread_id{0, 0};

  for (size_t i = 0; i < size; ++i) {
    // Iterate over each record
    gpu_patch_analysis_address_t *record = records + i;

    // <start, end>
    MemoryRange memory_range(record->start, record->end);
    uint64_t addr_start = record->start;
    uint64_t addr_end = addr_start;

    // Separate memories from a continous address region
    while (addr_end < memory_range.end) {
      MemoryRange cur_memory_range(addr_start, addr_start);
      auto iter = memory_map->prev(cur_memory_range);

      uint64_t memory_op_id = 0;
      int32_t memory_id = 0;
      uint64_t memory_size = 0;
      uint64_t memory_addr = 0;
      if (iter != memory_map->end()) {
        if (iter->first.start <= addr_start && iter->first.end > addr_start) {
          addr_end = MIN2(memory_range.end, iter->first.end);
          memory_op_id = iter->second->op_id;
          memory_id = iter->second->ctx_id;
          memory_size = addr_end - addr_start;
          memory_addr = addr_start;
          addr_start = iter->first.end;
        } else {
          // TODO(Keren): Investigate what are the causes
          // Prevent out of bound memory accesses
          break;
        }
      }

      if (memory_op_id == 0) {
        // Unknown memory object
        break;
      }

      Memory memory = Memory(memory_op_id, memory_id, memory_addr, memory_size);
      // XXX(Keren): Need to separate address analysis with value analysis
      for (auto aiter : analysis_enabled) {
        aiter.second->unit_access(kernel_id, thread_id, access_kind, memory, 0, 0, 0, 0,
                                  static_cast<GPUPatchFlags>(trace_data->flags));
      }
    }
  }

  return result;
}

static redshow_result_t trace_analyze_default(int32_t kernel_id, InstructionGraph *inst_graph,
                                              SymbolVector *symbols, MemoryMap *memory_map,
                                              gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  size_t size = trace_data->head_index;
  gpu_patch_record_t *records = reinterpret_cast<gpu_patch_record_t *>(trace_data->records);

  for (size_t i = 0; i < size; ++i) {
    // Iterate over each record
    gpu_patch_record_t *record = records + i;

    if (record->size == 0) {
      // Fast path, no thread active
      continue;
    }

    if (record->flags & GPU_PATCH_BLOCK_ENTER_FLAG) {
      // Skip analysis
    } else if (record->flags & GPU_PATCH_BLOCK_EXIT_FLAG) {
      // Remove temporal records
      for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
        if (record->active & (0x1u << j)) {
          uint32_t flat_thread_id =
              record->flat_thread_id / GPU_PATCH_WARP_SIZE * GPU_PATCH_WARP_SIZE + j;
          ThreadId thread_id{record->flat_block_id, flat_thread_id};
          for (auto aiter : analysis_enabled) {
            aiter.second->block_exit(thread_id);
          }
        }
      }
    } else {
      RealPC real_pc;

      auto ret = symbols->transform_pc(record->pc);
      if (ret.has_value()) {
        real_pc = ret.value();
      } else {
        result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
        return result;
      }

      // record->size * 8, byte to bits
      AccessKind access_kind;

      if (inst_graph->size() != 0) {
        // Accurate mode, when we have instruction information
        auto &inst = inst_graph->node(real_pc.cubin_offset);
        if (inst.access_kind.get() != NULL) {
          access_kind = *inst.access_kind;
        }
        // Fall back to default mode if failed
      }

      if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
        // Default mode, we identify every data as 64 bits unit size, 64 bits vec size, float type
        access_kind.data_type = default_data_type;
        if (access_kind.vec_size == 0) {
          access_kind.vec_size = record->size * 8;
          access_kind.unit_size = MIN2(32, access_kind.vec_size);
        }
      }

      // Reserved for debugging
      // std::cout << "function_index: " << real_pc.function_index << ", pc_offset: " <<
      //  real_pc.pc_offset << ", " << access_kind.to_string() << std::endl;
      // TODO: accelerate by handling all threads in a warp together
      for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
        if ((record->active & (0x1u << j)) == 0) {
          continue;
        }

        uint32_t flat_thread_id =
            record->flat_thread_id / GPU_PATCH_WARP_SIZE * GPU_PATCH_WARP_SIZE + j;
        ThreadId thread_id{record->flat_block_id, flat_thread_id};

        MemoryRange memory_range(record->address[j], record->address[j]);
        auto iter = memory_map->prev(memory_range);
        uint64_t memory_op_id = 0;
        int32_t memory_id = 0;
        uint64_t memory_size = 0;
        uint64_t memory_addr = 0;
        if (iter != memory_map->end()) {
          if (record->address[j] >= iter->second->memory_range.start &&
              record->address[j] + record->size <= iter->second->memory_range.end) {
            memory_op_id = iter->second->op_id;
            memory_id = iter->second->ctx_id;
            memory_size = iter->second->len;
            memory_addr = iter->second->memory_range.start;
          } else {
            // TODO(Keren): Investigate what are the causes
            // Prevent out of bound memory accesses
            continue;
          }
        }

        uint32_t stride = GLOBAL_MEMORY_OFFSET;
        if (memory_op_id == 0) {
          // XXX(Keren): memory_op_id == 1 ?
          // Memory object not found, it means the memory is local, shared, or allocated in an
          // unknown way
          if (record->flags & GPU_PATCH_LOCAL) {
            memory_op_id = REDSHOW_MEMORY_LOCAL;
            memory_id = LOCAL_MEMORY_CTX_ID;
            stride = LOCAL_MEMORY_OFFSET;
          } else if (record->flags & GPU_PATCH_SHARED) {
            memory_op_id = REDSHOW_MEMORY_SHARED;
            memory_id = SHARED_MEMORY_CTX_ID;
            stride = SHARED_MEMORY_OFFSET;
          } else {
            // Unknown allocation
          }
        }

        if (memory_op_id == 0) {
          // Unknown memory object
          continue;
        }

        Memory memory = Memory(memory_op_id, memory_id, memory_addr, memory_size);
        auto num_units = access_kind.vec_size / access_kind.unit_size;
        AccessKind unit_access_kind = access_kind;
        // We iterate through all the units such that every unit's vec_size = unit_size
        unit_access_kind.vec_size = unit_access_kind.unit_size;

        for (size_t m = 0; m < num_units; m++) {
          uint64_t value = 0;
          uint32_t byte_size = unit_access_kind.unit_size >> 3u;
          memcpy(&value, &record->value[j][m * byte_size], byte_size);
          // The 7th digit in float number's decimal part is partial valid, so we set the deault
          // approx level to REDSHOW_APPROX_MIN.
          value =
              unit_access_kind.value_to_basic_type(value, decimal_degree_f32, decimal_degree_f64);

          // Reserved for debug
          // std::cout << "thread: " << j << ", value: " << value << std::endl;

          for (auto aiter : analysis_enabled) {
            aiter.second->unit_access(kernel_id, thread_id, unit_access_kind, memory, record->pc,
                                      value, record->address[j], m,
                                      static_cast<GPUPatchFlags>(record->flags));
          }
        }
      }
    }
  }

  return result;
}

static redshow_result_t trace_analyze(uint32_t cpu_thread, uint32_t cubin_id, uint32_t mod_id,
                                      int32_t kernel_id, uint64_t host_op_id,
                                      gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  SymbolVector *symbols = NULL;
  InstructionGraph *inst_graph = NULL;
  // Cubin path is added just for debugging purpose
  std::string cubin_path;

  cubin_map.lock();
  if (!cubin_map.has(cubin_id) || !cubin_map.at(cubin_id).symbols.has(mod_id)) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    symbols = &(cubin_map.at(cubin_id).symbols.at(mod_id));
    inst_graph = &(cubin_map.at(cubin_id).inst_graph);
    cubin_path = cubin_map.at(cubin_id).path;
  }
  cubin_map.unlock();

  // Cubin not found, maybe in the cache map
  if (result == REDSHOW_ERROR_NOT_EXIST_ENTRY) {
    uint32_t nsymbols;
    uint64_t *symbol_pcs;
    const char *path;

    cubin_cache_map.lock();
    if (!cubin_cache_map.has(cubin_id)) {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    } else {
      auto &cubin_cache = cubin_cache_map.at(cubin_id);
      if (!cubin_cache.symbol_pcs.has(mod_id)) {
        result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
      } else {
        result = REDSHOW_SUCCESS;
        nsymbols = cubin_cache.nsymbols;
        symbol_pcs = cubin_cache.symbol_pcs.at(mod_id).get();
        path = cubin_cache.path.c_str();
      }
    }
    cubin_cache_map.unlock();

    if (result == REDSHOW_SUCCESS) {
      result = redshow_cubin_register(cubin_id, mod_id, nsymbols, symbol_pcs, path);
    }

    // Try fetch cubin again
    if (result == REDSHOW_SUCCESS) {
      cubin_map.lock();
      if (!cubin_map.has(cubin_id)) {
        result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
      } else {
        auto &cubin = cubin_map.at(cubin_id);
        if (!cubin.symbols.has(mod_id)) {
          result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
        } else {
          result = REDSHOW_SUCCESS;
          symbols = &(cubin.symbols.at(mod_id));
          inst_graph = &(cubin.inst_graph);
          cubin_path = cubin.path;
        }
      }
      cubin_map.unlock();
    }
  }

  if (result != REDSHOW_SUCCESS) {
    return result;
  }

  MemoryMap *memory_map = NULL;

  memory_snapshot.lock();
  auto snapshot_iter = memory_snapshot.prev(host_op_id);
  if (snapshot_iter == memory_snapshot.end()) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    memory_map = &(snapshot_iter->second);
  }
  memory_snapshot.unlock();

  // Memory snapshot not found
  if (result != REDSHOW_SUCCESS) {
    return result;
  }

  for (auto aiter : analysis_enabled) {
    aiter.second->analysis_begin(cpu_thread, kernel_id, cubin_id, mod_id,
                                 static_cast<GPUPatchType>(trace_data->type));
  }

  if (trace_data->type == GPU_PATCH_TYPE_DEFAULT) {
    result = trace_analyze_default(kernel_id, inst_graph, symbols, memory_map, trace_data);
  } else if (trace_data->type == GPU_PATCH_TYPE_ADDRESS_PATCH) {
    result = trace_analyze_address_patch(kernel_id, memory_map, trace_data);
  } else if (trace_data->type == GPU_PATCH_TYPE_ADDRESS_ANALYSIS) {
    result = trace_analyze_address_analysis(kernel_id, memory_map, trace_data);
  }

  for (auto aiter : analysis_enabled) {
    aiter.second->analysis_end(cpu_thread, kernel_id);
  }

  return result;
}

/*
 * Interface methods
 */

redshow_result_t redshow_output_dir_config(redshow_analysis_type_t analysis, const char *dir) {
  PRINT("\nredshow-> Enter redshow_output_dir_config\nanalysis: %u\ndir: %s\n", analysis, dir);

  if (dir) {
    output_dir[analysis] = std::string(dir);
  }

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_data_type_config(redshow_data_type_t data_type) {
  PRINT("\nredshow-> Enter redshow_data_type_config\ndata_type: %u\n", data_type);

  redshow_result_t result = REDSHOW_SUCCESS;

  switch (data_type) {
    case REDSHOW_DATA_FLOAT:
      default_data_type = REDSHOW_DATA_FLOAT;
      break;
    case REDSHOW_DATA_INT:
      default_data_type = REDSHOW_DATA_INT;
      break;
    default:
      result = REDSHOW_ERROR_NO_SUCH_DATA_TYPE;
      break;
  }

  return result;
};

redshow_result_t redshow_data_type_get(redshow_data_type_t *data_type) {
  *data_type = default_data_type;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_approx_level_config(redshow_approx_level_t level) {
  redshow_result_t result = REDSHOW_SUCCESS;

  switch (level) {
    case REDSHOW_APPROX_NONE:
      decimal_degree_f32 = VALID_FLOAT_DIGITS;
      decimal_degree_f64 = VALID_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MIN:
      decimal_degree_f32 = MIN_FLOAT_DIGITS;
      decimal_degree_f64 = MIN_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_LOW:
      decimal_degree_f32 = LOW_FLOAT_DIGITS;
      decimal_degree_f64 = LOW_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MID:
      decimal_degree_f32 = MID_FLOAT_DIGITS;
      decimal_degree_f64 = MID_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_HIGH:
      decimal_degree_f32 = HIGH_FLOAT_DIGITS;
      decimal_degree_f64 = HIGH_DOUBLE_DIGITS;
      break;
    case REDSHOW_APPROX_MAX:
      decimal_degree_f32 = MAX_FLOAT_DIGITS;
      decimal_degree_f64 = MAX_DOUBLE_DIGITS;
      break;
    default:
      result = REDSHOW_ERROR_NO_SUCH_APPROX;
      break;
  }

  return result;
}

redshow_result_t redshow_approx_get(int *degree_f32, int *degree_f64) {
  redshow_result_t result = REDSHOW_SUCCESS;

  *degree_f32 = decimal_degree_f32;
  *degree_f64 = decimal_degree_f64;

  return result;
}

redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type) {
  PRINT("\nredshow-> Enter redshow_analysis_enable\nanalysis_type: %u\n", analysis_type);

  redshow_result_t result = REDSHOW_SUCCESS;

  switch (analysis_type) {
    case REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY:
      analysis_enabled.emplace(REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY,
                               std::make_shared<SpatialRedundancy>());
      break;
    case REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY:
      analysis_enabled.emplace(REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY,
                               std::make_shared<TemporalRedundancy>());
      break;
    case REDSHOW_ANALYSIS_DATA_FLOW:
      analysis_enabled.emplace(REDSHOW_ANALYSIS_DATA_FLOW, std::make_shared<DataFlow>());
      break;
    case REDSHOW_ANALYSIS_VALUE_PATTERN:
      analysis_enabled.emplace(REDSHOW_ANALYSIS_VALUE_PATTERN, std::make_shared<ValuePattern>());
      break;
    case REDSHOW_ANALYSIS_MEMORY_PROFILE:
      analysis_enabled.emplace(REDSHOW_ANALYSIS_MEMORY_PROFILE, std::make_shared<MemoryProfile>());
      break;
    default:
      result = REDSHOW_ERROR_NO_SUCH_ANALYSIS;
      break;
  }

  return result;
}

redshow_result_t redshow_analysis_enabled(redshow_analysis_type_t analysis_type) {
  PRINT("\nredshow-> Enter redshow_analysis_enabled\nanalysis_type: %u\n", analysis_type);

  redshow_result_t result = REDSHOW_SUCCESS;

  if (!analysis_enabled.has(analysis_type)) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }

  return result;
}

redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type) {
  PRINT("\nredshow-> Enter redshow_analysis_disable\nanalysis_type: %u\n", analysis_type);

  analysis_enabled.erase(analysis_type);

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_analysis_config(redshow_analysis_type_t analysis_type,
                                         redshow_analysis_config_type_t config_type, bool enable) {
  PRINT(
      "\nredshow-> Enter redshow_analysis_config\nanalysis_type: %u, config_type: %u, enable: %u\n",
      analysis_type, config_type, enable);

  if (analysis_enabled.has(analysis_type)) {
    analysis_enabled[analysis_type]->config(config_type, enable);
  }

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_cubin_register(uint32_t cubin_id, uint32_t mod_id, uint32_t nsymbols,
                                        const uint64_t *symbol_pcs, const char *path) {
  PRINT("\nredshow-> Enter redshow_cubin_register\ncubin_id: %u\nmode_id: %u\npath: %s\n", cubin_id,
        mod_id, path);

  redshow_result_t result = REDSHOW_SUCCESS;

  InstructionGraph inst_graph;
  SymbolVector symbols(nsymbols);
  result = analyze_cubin(path, symbols, inst_graph);

  if (result == REDSHOW_SUCCESS || result == REDSHOW_ERROR_NO_SUCH_FILE) {
    // We must have found an instruction file, no matter nvdisasm failed or not
    // Assign symbol pc
    for (auto i = 0; i < nsymbols; ++i) {
      symbols[i].pc = symbol_pcs[i];
    }

    // Sort symbols by pc
    std::sort(symbols.begin(), symbols.end());

    cubin_map.lock();

    if (!cubin_map.has(cubin_id)) {
      cubin_map[cubin_id].cubin_id = cubin_id;
      cubin_map[cubin_id].path = path;
      cubin_map[cubin_id].inst_graph = inst_graph;
      result = REDSHOW_SUCCESS;
    } else if (cubin_map[cubin_id].symbols.find(mod_id) == cubin_map[cubin_id].symbols.end()) {
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_DUPLICATE_ENTRY;
    }
    if (result != REDSHOW_ERROR_DUPLICATE_ENTRY) {
      cubin_map[cubin_id].symbols[mod_id] = symbols;
    }

    cubin_map.unlock();
  }

  return result;
}

redshow_result_t redshow_cubin_cache_register(uint32_t cubin_id, uint32_t mod_id, uint32_t nsymbols,
                                              uint64_t *symbol_pcs, const char *path) {
  PRINT("\nredshow-> Enter redshow_cubin_cache_register\ncubin_id: %u\nmod_id: %u\npath: %s\n",
        cubin_id, mod_id, path);

  redshow_result_t result = REDSHOW_SUCCESS;

  cubin_cache_map.lock();
  if (!cubin_cache_map.has(cubin_id)) {
    auto &cubin_cache = cubin_cache_map[cubin_id];

    cubin_cache.cubin_id = cubin_id;
    cubin_cache.path = std::string(path);
    cubin_cache.nsymbols = nsymbols;
    result = REDSHOW_SUCCESS;
  } else if (cubin_cache_map[cubin_id].symbol_pcs.find(mod_id) ==
             cubin_cache_map[cubin_id].symbol_pcs.end()) {
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_DUPLICATE_ENTRY;
  }

  if (result != REDSHOW_ERROR_DUPLICATE_ENTRY) {
    auto *pcs = new uint64_t[nsymbols];
    cubin_cache_map[cubin_id].symbol_pcs[mod_id].reset(pcs);
    for (size_t i = 0; i < nsymbols; ++i) {
      pcs[i] = symbol_pcs[i];
    }
  }
  cubin_cache_map.unlock();

  return result;
}

redshow_result_t redshow_cubin_unregister(uint32_t cubin_id, uint32_t mod_id) {
  PRINT("\nredshow-> Enter redshow_cubin_unregister\ncubin_id: %u\n", cubin_id);

  redshow_result_t result = REDSHOW_SUCCESS;

  cubin_map.lock();
  if (cubin_map.has(cubin_id)) {
    cubin_map.at(cubin_id).symbols.erase(mod_id);
    if (cubin_map.at(cubin_id).symbols.size() == 0) {
      cubin_map.erase(cubin_id);
    }
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  cubin_map.unlock();

  return result;
}

redshow_result_t redshow_memory_register(int32_t memory_id, uint64_t host_op_id, uint64_t start,
                                         uint64_t end) {
  PRINT(
      "\nredshow-> Enter redshow_memory_register\nmemory_id: %d\nhost_op_id: %llu\nstart: %p\nend: "
      "%p\n",
      memory_id, host_op_id, start, end);

  redshow_result_t result = REDSHOW_SUCCESS;

  MemoryMap memory_map;
  MemoryRange memory_range(start, end);
  auto memory = std::make_shared<Memory>(host_op_id, memory_id, memory_range);

  memory_snapshot.lock();
  if (memory_snapshot.size() == 0) {
    // First snapshot
    memory_map[memory_range] = memory;
    memory_snapshot[host_op_id] = memory_map;
    result = REDSHOW_SUCCESS;
    PRINT("Register memory_id %d\n", memory_id);
  } else {
    // snapshot_iter's op_id <= host_op_id
    auto iter = memory_snapshot.prev(host_op_id);
    if (iter != memory_snapshot.end()) {
      // Take a snapshot
      memory_map = iter->second;
      if (!memory_map.has(memory_range)) {
        memory_map[memory_range] = memory;
        memory_snapshot[host_op_id] = memory_map;
        result = REDSHOW_SUCCESS;
        PRINT("Register memory_id %d\n", memory_id);
      } else {
        result = REDSHOW_ERROR_DUPLICATE_ENTRY;
      }
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  }
  memory_snapshot.unlock();

  if (result == REDSHOW_SUCCESS) {
    for (auto aiter : analysis_enabled) {
      aiter.second->op_callback(memory);
    }
  }

  return result;
}

redshow_result_t redshow_memory_unregister(uint64_t host_op_id, uint64_t start, uint64_t end) {
  PRINT("\nredshow-> Enter redshow_memory_unregister\nhost_op_id: %llu\nstart: %p\nend: %p\n",
        host_op_id, start, end);

  redshow_result_t result = REDSHOW_SUCCESS;

  MemoryMap memory_map;
  MemoryRange memory_range(start, end);
  auto memfree = std::make_shared<Memfree>(host_op_id, memory_range);

  memory_snapshot.lock();
  auto snapshot_iter = memory_snapshot.prev(host_op_id);
  if (snapshot_iter != memory_snapshot.end()) {
    // Take a snapshot
    memory_map = snapshot_iter->second;
    auto memory_map_iter = memory_map.find(memory_range);
    if (memory_map_iter != memory_map.end()) {
      // pass the ctx_id to memfree
      memfree->ctx_id = memory_map_iter->second->ctx_id;
      memory_map.erase(memory_map_iter);
      memory_snapshot[host_op_id] = memory_map;
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_snapshot.unlock();

  if (result == REDSHOW_SUCCESS) {
    for (auto aiter : analysis_enabled) {
      aiter.second->op_callback(memfree);
    }
  }

  return result;
}

redshow_result_t redshow_sub_memory_register(int32_t sub_memory_id, uint64_t host_op_id,
                                                 uint64_t start, uint64_t end) {
  PRINT("\nredshow-> Enter redshow_sub_memory_register\nmemory_id: %d\nhost_op_id: %llu\nstart: %p\nend: %p\n",
  sub_memory_id, host_op_id, start, end);

  redshow_result_t result = REDSHOW_SUCCESS;

  MemoryMap sub_memory_map;
  MemoryRange sub_memory_range(start, end);

  auto submemory = std::make_shared<Memory>(host_op_id, sub_memory_id, sub_memory_range);

  sub_memory_snapshot.lock();
    if (sub_memory_snapshot.size() == 0) {
        // First sub-memory snapshot
        sub_memory_map[sub_memory_range] = submemory;
        sub_memory_snapshot[host_op_id] = sub_memory_map;
        result = REDSHOW_SUCCESS;
        PRINT("Register sub-memory_id %d\n", sub_memory_id);
    } else {
        auto iter = sub_memory_snapshot.prev(host_op_id);
        if (iter != sub_memory_snapshot.end()) {
            // Take a snapshot
            sub_memory_map = iter->second;
            if (sub_memory_map.has(sub_memory_range)) {
                sub_memory_map[sub_memory_range] = submemory;
                sub_memory_snapshot[host_op_id] = sub_memory_map;
                result = REDSHOW_SUCCESS;
                PRINT("Register memory_id %d\n", sub_memory_id);
            } else {
                result = REDSHOW_ERROR_DUPLICATE_ENTRY;
            }
        } else {
            result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
        }
    }
    sub_memory_snapshot.unlock();

    bool is_submemory = true;
    if (result == REDSHOW_SUCCESS) {
        for (auto aiter : analysis_enabled) {

            aiter.second->op_callback(submemory, is_submemory);
        }
    }

    return result;    

}

redshow_result_t redshow_memory_query(uint64_t host_op_id, uint64_t start, int32_t *memory_id,
                                      uint64_t *memory_op_id, uint64_t *shadow_start,
                                      uint64_t *len) {
  PRINT("\nredshow-> Enter redshow_memory_query\nhost_op_id: %lu\nstart: %p\n", host_op_id, start);

  redshow_result_t result = REDSHOW_SUCCESS;

  MemoryRange memory_range(start, 0);

  memory_snapshot.lock();
  auto snapshot_iter = memory_snapshot.prev(host_op_id);
  if (snapshot_iter != memory_snapshot.end()) {
    auto &memory_map = snapshot_iter->second;
    auto memory_map_iter = memory_map.prev(memory_range);
    if (memory_map_iter != memory_map.end() && memory_map_iter->first.end >= start) {
      // Fall into the range, assume no memory overflow
      *memory_id = memory_map_iter->second->ctx_id;
      *memory_op_id = memory_map_iter->second->op_id;
      auto offset = start - memory_map_iter->first.start;
      *shadow_start = reinterpret_cast<uint64_t>(memory_map_iter->second->value.get()) + offset;
      *len = memory_map_iter->second->len;
      PRINT("memory_id: %d\nmemory_op_id: %llu\noffset %llu\nshadow: %p\nlen: %llu\n", *memory_id,
            *memory_op_id, offset, *shadow_start, *len);
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_snapshot.unlock();

  return result;
}

EXTERNC redshow_result_t redshow_memory_ranges_get(uint64_t host_op_id, uint64_t limit,
                                                   gpu_patch_analysis_address *start_end,
                                                   uint32_t *len) {
  PRINT("\nredshow-> Enter redshow_memory_ranges_get\nhost_op_id: %lu\nlimit: %lu\n", host_op_id,
        limit);

  redshow_result_t result = REDSHOW_SUCCESS;

  *len = 0;
  memory_snapshot.lock();
  auto snapshot_iter = memory_snapshot.prev(host_op_id);
  if (snapshot_iter != memory_snapshot.end()) {
    auto &memory_map = snapshot_iter->second;
    if (memory_map.size() != 0) {
      auto memory_map_iter = memory_map.begin();
      while (memory_map_iter != memory_map.end()) {
        start_end[(*len)].start = memory_map_iter->first.start;
        start_end[(*len)].end = memory_map_iter->first.end;
        ++(*len);
        ++memory_map_iter;
      }
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_snapshot.unlock();

  return result;
}

redshow_result_t redshow_memcpy_register(int32_t memcpy_id, uint64_t host_op_id, bool src_host,
                                         uint64_t src_start, bool dst_host, uint64_t dst_start,
                                         uint64_t len) {
  PRINT(
      "\nredshow-> Enter redshow_memcpy_register\nmemcpy_id: %d\nhost_op_id: "
      "%llu\nsrc_host: %d\nsrc_start: %llu\ndst_host: %d\ndst_start: "
      "%llu\nlen: %llu\n",
      memcpy_id, host_op_id, src_host, src_start, dst_host, dst_start, len);

  redshow_result_t result = REDSHOW_SUCCESS;

  i32 src_mem_id = 0;
  u64 src_mem_op_id = 0;
  u64 src_mem_addr = 0;
  u64 src_size = 0;
  i32 dst_mem_id = 0;
  u64 dst_mem_op_id = 0;
  u64 dst_mem_addr = 0;
  u64 dst_size = 0;

  if (src_host) {
    src_mem_addr = src_start;
    src_mem_id = REDSHOW_MEMORY_HOST;
    src_mem_op_id = REDSHOW_MEMORY_HOST;
  } else {
    redshow_memory_query(host_op_id, src_start, &src_mem_id, &src_mem_op_id, &src_mem_addr,
                         &src_size);
  }

  if (dst_host) {
    dst_mem_addr = dst_start;
    dst_mem_id = REDSHOW_MEMORY_HOST;
    dst_mem_op_id = REDSHOW_MEMORY_HOST;
  } else {
    redshow_memory_query(host_op_id, dst_start, &dst_mem_id, &dst_mem_op_id, &dst_mem_addr,
                         &dst_size);
  }

  if (dst_mem_addr != 0 && src_mem_addr != 0) {
    // Avoid memcpy to symbol without allocation
    auto memcpy = std::make_shared<Memcpy>(host_op_id, memcpy_id, src_mem_op_id, src_mem_addr,
                                           dst_mem_op_id, dst_mem_addr, len);

    for (auto aiter : analysis_enabled) {
      aiter.second->op_callback(memcpy);
    }
  }

  return result;
}

redshow_result_t redshow_memset_register(int32_t memset_id, uint64_t host_op_id, uint64_t start,
                                         uint32_t value, uint64_t len) {
  PRINT(
      "\nredshow-> Enter redshow_memset_register\nmemset_id: %d\nhost_op_id: %llu\nstart: "
      "%llu\nvalue: %u\nlen: %llu\n",
      memset_id, host_op_id, start, value, len);

  redshow_result_t result = REDSHOW_SUCCESS;

  i32 mem_id = 0;
  u64 mem_op_id = 0;
  u64 addr = 0;
  u64 size = 0;

  redshow_memory_query(host_op_id, start, &mem_id, &mem_op_id, &addr, &size);

  auto memset = std::make_shared<Memset>(host_op_id, memset_id, mem_op_id, addr, value, len);

  if (addr != 0) {
    for (auto aiter : analysis_enabled) {
      aiter.second->op_callback(memset);
    }
  }

  return result;
}

redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func) {
  log_data_callback = func;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_record_data_callback_register(redshow_record_data_callback_func func,
                                                       uint32_t pc_views, uint32_t mem_views) {
  record_data_callback = func;
  pc_views_limit = pc_views;
  mem_views_limit = mem_views;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_tool_dtoh_register(redshow_tool_dtoh_func func) {
  for (auto &aiter : analysis_enabled) {
    aiter.second->dtoh_register(func);
  }

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_pc_views_get(uint32_t *views) {
  *views = pc_views_limit;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_mem_views_get(uint32_t *views) {
  *views = mem_views_limit;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_kernel_begin(uint32_t cpu_thread, int32_t kernel_id, uint64_t host_op_id) {
  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_kernel_end(uint32_t cpu_thread, int32_t kernel_id, uint64_t host_op_id) {
  PRINT("\nredshow-> Enter redshow_kernel_end\ncpu_thread: %u\nkernel_id: %d\nhost_op_id: %llu\n",
        cpu_thread, kernel_id, host_op_id);

  // propose changes
  // read_memory_op_ids, write_memory_op_ids
  redshow_result_t result = REDSHOW_SUCCESS;

  auto kernel = std::make_shared<Kernel>(host_op_id, kernel_id, cpu_thread);

  for (auto aiter : analysis_enabled) {
    aiter.second->op_callback(kernel);
  }

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_analyze(uint32_t cpu_thread, uint32_t cubin_id, uint32_t mod_id,
                                 int32_t kernel_id, uint64_t host_op_id,
                                 gpu_patch_buffer_t *trace_data) {
  PRINT(
      "\nredshow-> Enter redshow_analyze\ncpu_thread: %u\ncubin_id: %u\nmod_id: %u\n"
      "kernel_id: %d\nhost_op_id: %llu\ntrace_data: %p\n",
      cpu_thread, cubin_id, mod_id, kernel_id, host_op_id, trace_data);

  redshow_result_t result;

  // Analyze trace_data
  result = trace_analyze(cpu_thread, cubin_id, mod_id, kernel_id, host_op_id, trace_data);

  if (result == REDSHOW_SUCCESS) {
    if (log_data_callback) {
      log_data_callback(kernel_id, trace_data);
      if (mini_host_op_id == 0) {
        mini_host_op_id = host_op_id;
      } else {
        mini_host_op_id = MIN2(mini_host_op_id, host_op_id);
      }
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_REGISTER_CALLBACK;
    }
  } else {
    PRINT("\nredshow-> Fail redshow_analyze result %d\n", result);
  }

  return result;
}

redshow_result_t redshow_analysis_begin() {
  PRINT("\nredshow-> Enter redshow_analysis_begin\n");

  mini_host_op_id = 0;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_analysis_end() {
  PRINT("\nredshow-> Enter redshow_analysis_end\n");

  redshow_result_t result;

  if (mini_host_op_id != 0 && !analysis_enabled.has(REDSHOW_ANALYSIS_DATA_FLOW)) {
    // Remove all the memory snapshots before mini_host_op_id
    Vector<uint64_t> ids;

    memory_snapshot.lock();
    uint64_t max_min_host_op_id = 0;
    for (auto &iter : memory_snapshot) {
      if (iter.first < mini_host_op_id) {
        ids.push_back(iter.first);
        max_min_host_op_id = MAX2(iter.first, max_min_host_op_id);
      }
    }
    // Maintain the largest snapshot
    for (auto &id : ids) {
      if (id == max_min_host_op_id) {
        continue;
      }
      memory_snapshot.erase(id);
    }
    memory_snapshot.unlock();

    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_REGISTER_CALLBACK;
  }

  return result;
}

redshow_result_t redshow_flush_thread(uint32_t cpu_thread) {
  PRINT("\nredshow-> Enter redshow_flush cpu_thread %u\n", cpu_thread);

  for (auto aiter : analysis_enabled) {
    aiter.second->flush_thread(cpu_thread, output_dir[aiter.first], cubin_map,
                               record_data_callback);
  }

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_flush() {
  PRINT("\nredshow-> Enter redshow_flush\n");

  for (auto aiter : analysis_enabled) {
    aiter.second->flush(output_dir[aiter.first], cubin_map, record_data_callback);
  }

  return REDSHOW_SUCCESS;
}
