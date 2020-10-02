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

#include "instruction.h"
#include "redundancy.h"
#include "utils.h"
#include "value_flow.h"

#ifdef DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

/*
 * Global data structures
 */

struct Cubin {
  uint32_t cubin_id;
  std::string path;
  // <mod_id, [symbols]>
  std::map<uint32_t, std::vector<redshow::Symbol>> symbols;
  redshow::InstructionGraph inst_graph;

  Cubin() = default;

  Cubin(uint32_t cubin_id, const char *path_, redshow::InstructionGraph &inst_graph)
      : cubin_id(cubin_id), path(path_), inst_graph(inst_graph) {}
};

static std::map<uint32_t, Cubin> cubin_map;
static std::mutex cubin_map_lock;

struct CubinCache {
  uint32_t cubin_id;
  uint32_t nsymbols;
  // TODO(Keren): refactor with shared_ptr
  std::map<uint32_t, uint64_t *> symbol_pcs;
  std::string path;

  CubinCache() = default;

  CubinCache(uint32_t cubin_id) : cubin_id(cubin_id), nsymbols(0) {}

  CubinCache(uint32_t cubin_id, const std::string &path)
      : cubin_id(cubin_id), path(path), nsymbols(0) {}

  ~CubinCache() {
    for (auto &iter : symbol_pcs) {
      if (iter.second) {
        delete[] iter.second;
      }
    }
  }
};

static std::map<uint32_t, CubinCache> cubin_cache_map;
static std::mutex cubin_cache_map_lock;

struct MemoryRange {
  uint64_t start;
  uint64_t end;

  MemoryRange() = default;

  MemoryRange(uint64_t start, uint64_t end) : start(start), end(end) {}

  bool operator<(const MemoryRange &other) const { return start < other.start; }
};

struct Memory {
  MemoryRange memory_range;
  int32_t memory_id;
  uint64_t memory_op_id;
  std::shared_ptr<uint8_t> value;

  Memory() = default;

  Memory(MemoryRange &memory_range, int32_t memory_id, uint64_t memory_op_id)
      : memory_range(memory_range),
        memory_id(memory_id),
        memory_op_id(memory_op_id),
        value(new uint8_t[memory_range.end - memory_range.start],
              std::default_delete<uint8_t[]>()) {}
};

typedef std::map<MemoryRange, Memory> MemoryMap;
static std::map<uint64_t, MemoryMap> memory_snapshot;
static std::mutex memory_snapshot_lock;

struct Kernel {
  int32_t kernel_id;
  uint32_t cubin_id;
  uint32_t mod_id;
  uint32_t func_index;
  uint64_t func_addr;

  // Spatial redundancy
  redshow::SpatialTrace read_spatial_trace;
  redshow::SpatialTrace write_spatial_trace;

  // Temporal redundancy
  redshow::TemporalTrace read_temporal_trace;
  redshow::PCPairs read_pc_pairs;
  redshow::PCAccessCount read_pc_count;
  redshow::TemporalTrace write_temporal_trace;
  redshow::PCPairs write_pc_pairs;
  redshow::PCAccessCount write_pc_count;

  // value flow

  Kernel() = default;

  Kernel(int32_t kernel_id, uint32_t cubin_id, uint32_t func_index, uint64_t func_addr)
      : kernel_id(kernel_id), cubin_id(cubin_id), func_index(func_index), func_addr(func_addr) {}
};

static std::map<uint32_t, std::map<int32_t, Kernel>> kernel_map;
static std::mutex kernel_map_lock;

// Memcpy and memset must be analyzed by runtime threads.
// Therefore we cannot use a static structure in value_flow.cpp
struct Memcpy {
  uint64_t memcpy_id;
  int32_t memcpy_op_id;
  uint64_t src_memory_op_id;
  uint64_t dst_memory_op_id;
  std::string hash;
  double redundancy;

  Memcpy() = default;

  Memcpy(uint64_t memcpy_id, int32_t memcpy_op_id, uint32_t src_memory_op_id, uint32_t dst_memory_op_id,
         const std::string &hash, double redundancy)
      : memcpy_id(memcpy_id),
        memcpy_op_id(memcpy_op_id),
        src_memory_op_id(src_memory_op_id),
        dst_memory_op_id(dst_memory_op_id),
        hash(hash),
        redundancy(redundancy) {}
};

static std::vector<Memcpy> memcpys;
static std::mutex memcpys_lock;

struct Memset {
  uint64_t memset_id;
  int32_t memset_op_id;
  uint64_t memory_op_id;
  const std::string hash;
  double redundancy;

  Memset() = default;

  Memset(uint64_t memset_id, int32_t memset_op_id, uint64_t memory_op_id, const std::string &hash,
         double redundancy)
      : memset_id(memset_id),
        memset_op_id(memset_op_id),
        memory_op_id(memory_op_id),
        hash(hash),
        redundancy(redundancy) {}
};

static std::vector<Memset> memsets;
static std::mutex memsets_lock;

static redshow::ValueFlowGraph value_flow_graph;
static std::mutex value_flow_graph_lock;

static std::set<redshow_analysis_type_t> analysis_enabled;

static redshow_log_data_callback_func log_data_callback = NULL;

static redshow_record_data_callback_func record_data_callback = NULL;

static __thread uint64_t mini_host_op_id = 0;

static uint32_t pc_views_limit = 0;
static uint32_t mem_views_limit = 0;

static int decimal_degree_f32 = VALID_FLOAT_DIGITS;
static int decimal_degree_f64 = VALID_DOUBLE_DIGITS;

static redshow_data_type_t default_data_type = REDSHOW_DATA_FLOAT;

static redshow_result_t analyze_cubin(const char *path, std::vector<redshow::Symbol> &symbols,
                                      redshow::InstructionGraph &inst_graph) {
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
      if (parse_instructions(inst_path, symbols, inst_graph)) {
        result = REDSHOW_SUCCESS;
      } else {
        result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
      }
    }
  }

  return result;
}

static redshow_result_t transform_pc(std::vector<redshow::Symbol> &symbols, uint64_t pc,
                                     uint32_t &function_index, uint64_t &cubin_offset,
                                     uint64_t &pc_offset) {
  redshow_result_t result = REDSHOW_SUCCESS;

  redshow::Symbol symbol(pc);

  auto symbols_iter = std::upper_bound(symbols.begin(), symbols.end(), symbol);

  if (symbols_iter != symbols.begin()) {
    --symbols_iter;
    pc_offset = pc - symbols_iter->pc;
    cubin_offset = pc_offset + symbols_iter->cubin_offset;
    function_index = symbols_iter->index;
    // PRINT("Transform %p to %u: <%p, %p>\n", pc, function_index, pc_offset, cubin_offset);
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }

  return result;
}

static redshow_result_t transform_data_views(std::vector<redshow::Symbol> &symbols,
                                             redshow_record_data_t &record_data) {
  // Transform pcs
  for (auto i = 0; i < record_data.num_views; ++i) {
    uint64_t pc = record_data.views[i].pc_offset;
    uint32_t function_index = 0;
    uint64_t cubin_offset = 0;
    uint64_t pc_offset = 0;
    transform_pc(symbols, pc, function_index, cubin_offset, pc_offset);
    record_data.views[i].function_index = function_index;
    record_data.views[i].pc_offset = pc_offset;
  }
}

static redshow_result_t transform_temporal_statistics(uint32_t cubin_id,
                                                      std::vector<redshow::Symbol> &symbols,
                                                      redshow::TemporalStatistics &temporal_stats) {
  for (auto &temp_stat_iter : temporal_stats) {
    for (auto &real_pc_pair : temp_stat_iter.second) {
      auto &to_real_pc = real_pc_pair.to_pc;
      auto &from_real_pc = real_pc_pair.from_pc;
      uint32_t function_index = 0;
      uint64_t cubin_offset = 0;
      uint64_t pc_offset = 0;
      // to_real_pc
      transform_pc(symbols, to_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
      to_real_pc.cubin_id = cubin_id;
      to_real_pc.function_index = function_index;
      to_real_pc.pc_offset = pc_offset;
      // from_real_pc
      transform_pc(symbols, from_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
      from_real_pc.cubin_id = cubin_id;
      from_real_pc.function_index = function_index;
      from_real_pc.pc_offset = pc_offset;
    }
  }
}

static redshow_result_t transform_spatial_statistics(uint32_t cubin_id,
                                                     std::vector<redshow::Symbol> &symbols,
                                                     redshow::SpatialStatistics &spatial_stats) {
  for (auto &spatial_stat_iter : spatial_stats) {
    for (auto &pc_iter : spatial_stat_iter.second) {
      for (auto &real_pc_pair : pc_iter.second) {
        auto &to_real_pc = real_pc_pair.to_pc;
        uint32_t function_index = 0;
        uint64_t cubin_offset = 0;
        uint64_t pc_offset = 0;
        // to_real_pc
        transform_pc(symbols, to_real_pc.pc_offset, function_index, cubin_offset, pc_offset);
        to_real_pc.cubin_id = cubin_id;
        to_real_pc.function_index = function_index;
        to_real_pc.pc_offset = pc_offset;
      }
    }
  }
}

static redshow_result_t trace_analyze(Kernel &kernel, uint64_t host_op_id,
                                      gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  auto cubin_id = kernel.cubin_id;
  auto mod_id = kernel.mod_id;
  auto kernel_id = kernel.kernel_id;
  auto &read_spatial_trace = kernel.read_spatial_trace;
  auto &write_spatial_trace = kernel.write_spatial_trace;
  auto &read_temporal_trace = kernel.read_temporal_trace;
  auto &read_pc_pairs = kernel.read_pc_pairs;
  auto &write_temporal_trace = kernel.write_temporal_trace;
  auto &write_pc_pairs = kernel.write_pc_pairs;
  auto &read_pc_count = kernel.read_pc_count;
  auto &write_pc_count = kernel.write_pc_count;
  std::vector<redshow::Symbol> *symbols = NULL;
  redshow::InstructionGraph *inst_graph = NULL;
  // Cubin path is added just for debugging purpose
  std::string cubin_path;

  cubin_map_lock.lock();
  if (cubin_map.find(cubin_id) == cubin_map.end() ||
      cubin_map[cubin_id].symbols.find(mod_id) == cubin_map[cubin_id].symbols.end()) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    symbols = &(cubin_map[cubin_id].symbols[mod_id]);
    inst_graph = &(cubin_map[cubin_id].inst_graph);
    cubin_path = cubin_map[cubin_id].path;
  }
  cubin_map_lock.unlock();

  // Cubin not found, maybe in the cache map
  if (result == REDSHOW_ERROR_NOT_EXIST_ENTRY) {
    uint32_t nsymbols;
    uint64_t *symbol_pcs;
    const char *path;

    cubin_cache_map_lock.lock();
    if (cubin_cache_map.find(cubin_id) == cubin_cache_map.end()) {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    } else {
      auto &cubin_cache = cubin_cache_map[cubin_id];
      if (cubin_cache.symbol_pcs.find(mod_id) == cubin_cache.symbol_pcs.end()) {
        result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
      } else {
        result = REDSHOW_SUCCESS;
        nsymbols = cubin_cache.nsymbols;
        symbol_pcs = cubin_cache.symbol_pcs.at(mod_id);
        path = cubin_cache.path.c_str();
      }
    }
    cubin_cache_map_lock.unlock();

    if (result == REDSHOW_SUCCESS) {
      result = redshow_cubin_register(cubin_id, mod_id, nsymbols, symbol_pcs, path);
    }

    // Try fetch cubin again
    if (result == REDSHOW_SUCCESS) {
      cubin_map_lock.lock();
      if (cubin_map.find(cubin_id) == cubin_map.end()) {
        result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
      } else {
        auto &cubin = cubin_map[cubin_id];
        if (cubin.symbols.find(mod_id) == cubin.symbols.end()) {
          result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
        } else {
          result = REDSHOW_SUCCESS;
          symbols = &(cubin.symbols.at(mod_id));
          inst_graph = &(cubin.inst_graph);
          cubin_path = cubin.path;
        }
      }
      cubin_map_lock.unlock();
    }
  }

  if (result != REDSHOW_SUCCESS) {
    return result;
  }

  MemoryMap *memory_map = NULL;

  memory_snapshot_lock.lock();
  auto snapshot_iter = memory_snapshot.upper_bound(host_op_id);
  if (snapshot_iter == memory_snapshot.begin()) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    --snapshot_iter;
    memory_map = &(snapshot_iter->second);
  }
  memory_snapshot_lock.unlock();

  // Memory snapshot not found
  if (result != REDSHOW_SUCCESS) {
    return result;
  }

  // Records involved memory ids
  std::set<uint64_t> read_memory_op_ids;
  std::set<uint64_t> write_memory_op_ids;
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
      if (analysis_enabled.find(REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) != analysis_enabled.end()) {
        for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
          if (record->active & (0x1u << j)) {
            uint32_t flat_thread_id =
                record->flat_thread_id / GPU_PATCH_WARP_SIZE * GPU_PATCH_WARP_SIZE + j;
            ThreadId thread_id{record->flat_block_id, flat_thread_id};
            read_temporal_trace.erase(thread_id);
            write_temporal_trace.erase(thread_id);
          }
        }
      }
    } else {
      uint32_t function_index = 0;
      uint64_t cubin_offset = 0;
      uint64_t pc_offset = 0;
      transform_pc(*symbols, record->pc, function_index, cubin_offset, pc_offset);

      // record->size * 8, byte to bits
      redshow::AccessKind access_kind;

      if (inst_graph->size() != 0) {
        // Accurate mode, when we have instruction information
        auto &inst = inst_graph->node(cubin_offset);
        if (inst.access_kind.get() != NULL) {
          access_kind = *inst.access_kind;
        }
        // Fall back to default mode if failed
      }

      if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
        // Default mode, we identify every data as 32 bits unit size, 32 bits vec size, float type
        access_kind.data_type = default_data_type;
        access_kind.vec_size = record->size * 8;
        access_kind.unit_size = MIN2(GPU_PATCH_WARP_SIZE, access_kind.vec_size * 8);
      }

      //// Reserved for debugging
      // std::cout << "function_index: " << function_index << ", pc_offset: " <<
      //  pc_offset << ", " << access_kind.to_string() << std::endl;
      // TODO: accelerate by handling all threads in a warp together
      for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
        if ((record->active & (0x1u << j)) == 0) {
          continue;
        }

        uint32_t flat_thread_id =
            record->flat_thread_id / GPU_PATCH_WARP_SIZE * GPU_PATCH_WARP_SIZE + j;
        ThreadId thread_id{record->flat_block_id, flat_thread_id};

        MemoryRange memory_range(record->address[j], record->address[j]);
        auto iter = memory_map->upper_bound(memory_range);
        uint64_t memory_op_id = 0;
        if (iter != memory_map->begin()) {
          --iter;
          memory_op_id = iter->second.memory_op_id;
        }

        uint32_t address_offset = GLOBAL_MEMORY_OFFSET;
        if (memory_op_id == 0) {
          // XXX(Keren): memory_op_id == 1 ?
          // Memory object not found, it means the memory is local, shared, or allocated in an
          // unknown way
          if (record->flags & GPU_PATCH_LOCAL) {
            memory_op_id = REDSHOW_MEMORY_SHARED;
            address_offset = LOCAL_MEMORY_OFFSET;
          } else if (record->flags & GPU_PATCH_SHARED) {
            memory_op_id = REDSHOW_MEMORY_LOCAL;
            address_offset = SHARED_MEMORY_OFFSET;
          } else {
            // Unknown allocation
          }
        }

        if (memory_op_id == 0) {
          // Unknown memory object
          continue;
        }

        auto num_units = access_kind.vec_size / access_kind.unit_size;
        redshow::AccessKind unit_access_kind = access_kind;
        // We iterate through all the units such that every unit's vec_size = unit_size
        unit_access_kind.vec_size = unit_access_kind.unit_size;

        if (record->flags & GPU_PATCH_READ) {
          read_pc_count[record->pc] += num_units;
        } else {
          write_pc_count[record->pc] += num_units;
        }

        for (size_t m = 0; m < num_units; m++) {
          uint64_t value = 0;
          uint32_t byte_size = unit_access_kind.unit_size >> 3u;
          memcpy(&value, &record->value[j][m * byte_size], byte_size);
          value =
              unit_access_kind.value_to_basic_type(value, decimal_degree_f32, decimal_degree_f64);

          for (auto analysis : analysis_enabled) {
            if (analysis == REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) {
              if (record->flags & GPU_PATCH_READ) {
                redshow::update_spatial_trace(record->pc, value, memory_op_id, unit_access_kind,
                                              read_spatial_trace);
              } else {
                redshow::update_spatial_trace(record->pc, value, memory_op_id, unit_access_kind,
                                              write_spatial_trace);
              }
            } else if (analysis == REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) {
              if (record->flags & GPU_PATCH_READ) {
                redshow::update_temporal_trace(
                    record->pc, thread_id, record->address[j] + m * address_offset, value,
                    unit_access_kind, read_temporal_trace, read_pc_pairs);
              } else {
                redshow::update_temporal_trace(
                    record->pc, thread_id, record->address[j] + m * address_offset, value,
                    unit_access_kind, write_temporal_trace, write_pc_pairs);
              }
            } else if (analysis == REDSHOW_ANALYSIS_VALUE_FLOW) {
              if (record->flags & GPU_PATCH_READ) {
                read_memory_op_ids.insert(memory_op_id);
              } else {
                write_memory_op_ids.insert(memory_op_id);
              }
            } else {
              // Pass
            }
          }
        }
      }
    }
  }

  if (analysis_enabled.find(REDSHOW_ANALYSIS_VALUE_FLOW) != analysis_enabled.end()) {
    // Hold this lock during analysis
    value_flow_graph_lock.lock();

    // Add a calling context node
    if (!value_flow_graph.has_node(kernel_id)) {
      redshow::ValueFlowNode node(redshow::VALUE_FLOW_NODE_KERNEL, kernel_id);
      value_flow_graph.add_node(kernel_id, node);
    }

    for (auto memory_op_id : read_memory_op_ids) {
      if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
        auto node_id = value_flow_graph.op_node_id(memory_op_id);
        // Link a pure read edge between two calling contexts
        if (!value_flow_graph.has_edge(node_id, kernel_id, redshow::VALUE_FLOW_EDGE_ORDER)) {
          value_flow_graph.add_edge(node_id, kernel_id, redshow::VALUE_FLOW_EDGE_READ);
        }
      }
    }

    for (auto memory_op_id : write_memory_op_ids) {
      if (memory_op_id != REDSHOW_MEMORY_SHARED && memory_op_id != REDSHOW_MEMORY_LOCAL) {
        auto node_id = value_flow_graph.op_node_id(memory_op_id);
        value_flow_graph.remove_edge(node_id, kernel_id, redshow::VALUE_FLOW_EDGE_READ);
        // Point the operation to the calling context
        value_flow_graph.update_op_node(memory_op_id, kernel_id);
      }
    }

    value_flow_graph_lock.unlock();
  }

  return result;
}

/*
 * Interface methods
 */

redshow_result_t redshow_data_type_config(redshow_data_type_t data_type) {
  PRINT("\nredshow->Enter redshow_data_type_config\ndata_type: %u\n", data_type);

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
  PRINT("\nredshow->Enter redshow_data_type_get\n");

  *data_type = default_data_type;
  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_approx_level_config(redshow_approx_level_t level) {
  PRINT("\nredshow->Enter redshow_approx_level_config\nlevel: %u\n", level);

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

redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type) {
  PRINT("\nredshow->Enter redshow_analysis_enable\nanalysis_type: %u\n", analysis_type);

  analysis_enabled.insert(analysis_type);

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type) {
  PRINT("\nredshow->Enter redshow_analysis_disable\nanalysis_type: %u\n", analysis_type);

  analysis_enabled.erase(analysis_type);

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_cubin_register(uint32_t cubin_id, uint32_t mod_id, uint32_t nsymbols,
                                        uint64_t *symbol_pcs, const char *path) {
  PRINT("\nredshow->Enter redshow_cubin_register\ncubin_id: %u\nmode_id: %u\npath: %s\n", cubin_id,
        mod_id, path);

  redshow_result_t result;

  redshow::InstructionGraph inst_graph;
  std::vector<redshow::Symbol> symbols(nsymbols);
  result = analyze_cubin(path, symbols, inst_graph);

  if (result == REDSHOW_SUCCESS || result == REDSHOW_ERROR_NO_SUCH_FILE) {
    // We must have found an instruction file, no matter nvdisasm failed or not
    // Assign symbol pc
    for (auto i = 0; i < nsymbols; ++i) {
      symbols[i].pc = symbol_pcs[i];
    }

    // Sort symbols by pc
    std::sort(symbols.begin(), symbols.end());

    cubin_map_lock.lock();

    if (cubin_map.find(cubin_id) == cubin_map.end()) {
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

    cubin_map_lock.unlock();
  }

  return result;
}

redshow_result_t redshow_cubin_cache_register(uint32_t cubin_id, uint32_t mod_id, uint32_t nsymbols,
                                              uint64_t *symbol_pcs, const char *path) {
  PRINT("\nredshow->Enter redshow_cubin_cache_register\ncubin_id: %u\nmod_id: %u\npath: %s\n",
        cubin_id, mod_id, path);

  redshow_result_t result = REDSHOW_SUCCESS;

  cubin_cache_map_lock.lock();
  if (cubin_cache_map.find(cubin_id) == cubin_cache_map.end()) {
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
    cubin_cache_map[cubin_id].symbol_pcs[mod_id] = new uint64_t[nsymbols];
    for (size_t i = 0; i < nsymbols; ++i) {
      cubin_cache_map[cubin_id].symbol_pcs[mod_id][i] = symbol_pcs[i];
    }
  }
  cubin_cache_map_lock.unlock();

  return result;
}

redshow_result_t redshow_cubin_unregister(uint32_t cubin_id, uint32_t mod_id) {
  PRINT("\nredshow->Enter redshow_cubin_unregister\ncubin_id: %u\n", cubin_id);

  redshow_result_t result;

  cubin_map_lock.lock();
  if (cubin_map.find(cubin_id) != cubin_map.end()) {
    cubin_map[cubin_id].symbols.erase(mod_id);
    if (cubin_map[cubin_id].symbols.size() == 0) {
      cubin_map.erase(cubin_id);
    }
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  cubin_map_lock.unlock();

  return result;
}

redshow_result_t redshow_memory_register(int32_t memory_id, uint64_t host_op_id, uint64_t start,
                                         uint64_t end) {
  PRINT(
      "\nredshow->Enter redshow_memory_register\nmemory_id: %d\nhost_op_id: %llu\nstart: %p\nend: "
      "%p\n",
      memory_id, host_op_id, start, end);

  redshow_result_t result;
  MemoryMap memory_map;
  MemoryRange memory_range(start, end);

  memory_snapshot_lock.lock();
  if (memory_snapshot.size() == 0) {
    // First snapshot
    Memory memory(memory_range, memory_id, host_op_id);
    memory_map[memory_range] = memory;
    memory_snapshot[host_op_id] = memory_map;
    result = REDSHOW_SUCCESS;
    PRINT("host_op_id: %llu\nshadow: %p\n", host_op_id, memory.value.get());
  } else {
    auto iter = memory_snapshot.upper_bound(host_op_id);
    if (iter != memory_snapshot.begin()) {
      --iter;
      // Take a snapshot
      memory_map = iter->second;
      if (memory_map.find(memory_range) == memory_map.end()) {
        Memory memory(memory_range, memory_id, host_op_id);
        memory_map[memory_range] = memory;
        memory_snapshot[host_op_id] = memory_map;
        result = REDSHOW_SUCCESS;
        PRINT("host_op_id: %llu\nshadow: %p\n", host_op_id, memory.value.get());
      } else {
        result = REDSHOW_ERROR_DUPLICATE_ENTRY;
      }
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  }
  memory_snapshot_lock.unlock();

  if (result == REDSHOW_SUCCESS) {
    if (analysis_enabled.find(REDSHOW_ANALYSIS_VALUE_FLOW) != analysis_enabled.end()) {
      value_flow_graph_lock.lock();

      if (!value_flow_graph.has_node(memory_id)) {
        // Allocate calling context node
        redshow::ValueFlowNode node(redshow::VALUE_FLOW_NODE_ALLOC, memory_id);
        value_flow_graph.add_node(memory_id, node);
      }

      // Point the operation to the calling context
      value_flow_graph.update_op_node(host_op_id, memory_id);
    
      value_flow_graph_lock.unlock();
    }
  }

  return result;
}

redshow_result_t redshow_memory_unregister(uint64_t start, uint64_t end, uint64_t host_op_id) {
  PRINT("\nredshow->Enter redshow_memory_unregister\nstart: %p\nend: %p\nhost_op_id: %llu\n", start, end, host_op_id);

  redshow_result_t result;
  MemoryMap memory_map;
  MemoryRange memory_range(start, end);

  memory_snapshot_lock.lock();
  auto snapshot_iter = memory_snapshot.upper_bound(host_op_id);
  if (snapshot_iter != memory_snapshot.begin()) {
    --snapshot_iter;
    // Take a snapshot
    memory_map = snapshot_iter->second;
    auto memory_map_iter = memory_map.find(memory_range);
    if (memory_map_iter != memory_map.end()) {
      memory_map.erase(memory_map_iter);
      memory_snapshot[host_op_id] = memory_map;
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_snapshot_lock.unlock();

  return result;
}

redshow_result_t redshow_memory_query(uint64_t host_op_id, uint64_t start, int32_t *memory_id,
                                      uint64_t *memory_op_id, uint64_t *shadow_start, uint64_t *len) {
  PRINT("\nredshow->Enter redshow_memory_query\nhost_op_id: %lu\nstart: %p\n", host_op_id, start);

  redshow_result_t result;
  MemoryRange memory_range(start, 0);

  memory_snapshot_lock.lock();
  auto snapshot_iter = memory_snapshot.upper_bound(host_op_id);
  if (snapshot_iter != memory_snapshot.begin()) {
    --snapshot_iter;
    auto &memory_map = snapshot_iter->second;
    auto memory_map_iter = memory_map.find(memory_range);
    if (memory_map_iter != memory_map.end()) {
      *memory_id = memory_map_iter->second.memory_id;
      *memory_op_id = memory_map_iter->second.memory_op_id;
      *shadow_start = reinterpret_cast<uint64_t>(memory_map_iter->second.value.get());
      *len = memory_map_iter->first.end - memory_map_iter->first.start;
      PRINT("memory_id: %d\nmemory_op_id: %llu\nshadow: %p\nlen: %llu\n", *memory_id, *memory_op_id, *shadow_start, *len);
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_snapshot_lock.unlock();

  return result;
}

redshow_result_t redshow_memcpy_register(int32_t memcpy_id, uint64_t host_op_id,
                                         uint64_t src_memory_op_id, uint64_t src_start,
                                         uint64_t dst_memory_op_id, uint64_t dst_start,
                                         uint64_t len) {
  PRINT(
      "\nredshow->Enter redshow_memcpy_register\nmemcpy_id: %d\nhost_op_id: %llu\nsrc_memory_op_id: "
      "%llu\nsrc_start: %p\ndst_memory_op_id: %llu\ndst_start: %p\nlen: %llu\n",
      memcpy_id, host_op_id, src_memory_op_id, src_start, dst_memory_op_id, dst_start, len);

  redshow_result_t result = REDSHOW_SUCCESS;

  if (analysis_enabled.find(REDSHOW_ANALYSIS_VALUE_FLOW) != analysis_enabled.end()) {
    std::string hash = redshow::compute_memory_hash(src_start, len);
    double redundancy = redshow::compute_memcpy_redundancy(dst_start, src_start, len);

    value_flow_graph_lock.lock();

    // Allocate a calling context node
    redshow::ValueFlowNode node(redshow::VALUE_FLOW_NODE_MEMCPY, memcpy_id);
    value_flow_graph.add_node(memcpy_id, node);

    if (dst_memory_op_id != REDSHOW_MEMORY_HOST) {
      // If the dst node is for GPU memory, point the operation to the calling context node
      value_flow_graph.update_op_node(dst_memory_op_id, memcpy_id);

      if (src_memory_op_id != REDSHOW_MEMORY_HOST) {
        // If the src node is for GPU memory, linking a read edge between two nodes
        value_flow_graph.add_op_edge(src_memory_op_id, dst_memory_op_id, redshow::VALUE_FLOW_EDGE_READ);
      }
    }

    value_flow_graph_lock.unlock();

    memcpys_lock.lock();

    if (hash != "") {
      memcpys.emplace_back(std::move(
          Memcpy(memcpy_id, host_op_id, src_memory_op_id, dst_memory_op_id, hash, redundancy)));
    }

    memcpys_lock.unlock();
  }

  return result;
}

redshow_result_t redshow_memset_register(int32_t memset_id, uint64_t host_op_id, uint64_t memory_op_id,
                                         uint64_t shadow_start, uint32_t value, uint64_t len) {
  PRINT(
      "\nredshow->Enter redshow_memset_register\nmemset_id: %d\nhost_op_id: %llu\nmemory_op_id: "
      "%llu\nshadow_start: %p\nvalue: %u\nlen: %llu\n",
      memset_id, host_op_id, memory_op_id, shadow_start, value, len);

  redshow_result_t result = REDSHOW_SUCCESS;

  if (analysis_enabled.find(REDSHOW_ANALYSIS_VALUE_FLOW) != analysis_enabled.end()) {
    std::string hash = redshow::compute_memory_hash(shadow_start, len);
    double redundancy = redshow::compute_memset_redundancy(shadow_start, value, len);

    value_flow_graph_lock.lock();
    
    // Add a calling context node
    redshow::ValueFlowNode node(redshow::VALUE_FLOW_NODE_MEMCPY, memset_id);
    value_flow_graph.add_node(memset_id, node);
    // Point the operation to the calling context
    value_flow_graph.update_op_node(memory_op_id, memset_id);

    value_flow_graph_lock.unlock();

    memsets_lock.lock();

    if (hash != "") {
      memsets.emplace_back(std::move(Memset(memset_id, host_op_id, memory_op_id, hash, redundancy)));
    }

    memsets_lock.unlock();
  }

  return result;
}

redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func) {
  log_data_callback = func;
}

redshow_result_t redshow_record_data_callback_register(redshow_record_data_callback_func func,
                                                       uint32_t pc_views, uint32_t mem_views) {
  record_data_callback = func;
  pc_views_limit = pc_views;
  mem_views_limit = mem_views;
}


redshow_result_t redshow_analyze(uint32_t thread_id, uint32_t cubin_id, uint32_t mod_id,
                                 int32_t kernel_id, uint64_t host_op_id,
                                 gpu_patch_buffer_t *trace_data) {
  PRINT(
      "\nredshow->Enter redshow_analyze\nthread_id: %u\ncubin_id: %u\nmod_id: %u\n"
      "kernel_id: %d\nhost_op_id: %llu\ntrace_data: %p\n",
      thread_id, cubin_id, mod_id, kernel_id, host_op_id, trace_data);

  redshow_result_t result;

  kernel_map_lock.lock();

  auto &thread_kernel_map = kernel_map[thread_id];

  kernel_map_lock.unlock();

  // Analyze trace_data
  Kernel &kernel = thread_kernel_map[kernel_id];
  // First time mutable only
  kernel.kernel_id = kernel_id;
  kernel.cubin_id = cubin_id;
  kernel.mod_id = mod_id;

  result = trace_analyze(kernel, host_op_id, trace_data);

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
    PRINT("\nredshow->Fail redshow_analyze result %d\n", result);
  }

  return result;
}

redshow_result_t redshow_analysis_begin() {
  PRINT("\nredshow->Enter redshow_analysis_begin\n");

  mini_host_op_id = 0;

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_analysis_end() {
  PRINT("\nredshow->Enter redshow_analysis_end\n");

  redshow_result_t result;

  if (mini_host_op_id != 0) {
    // Remove all the memory snapshots before mini_host_op_id
    std::vector<uint64_t> ids;

    memory_snapshot_lock.lock();
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
    memory_snapshot_lock.unlock();

    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
  }

  return result;
}

void redundancy_flush(uint32_t thread_id, std::map<int32_t, Kernel> &thread_kernel_map) {
  redshow_record_data_t record_data;

  record_data.views = new redshow_record_view_t[pc_views_limit]();

  u64 thread_count = 0;
  u64 thread_read_temporal_count = 0;
  u64 thread_write_temporal_count = 0;
  u64 thread_read_spatial_count = 0;
  u64 thread_write_spatial_count = 0;
  for (auto &kernel_iter : thread_kernel_map) {
    auto kernel_id = kernel_iter.first;
    auto &kernel = kernel_iter.second;
    auto cubin_id = kernel.cubin_id;
    auto mod_id = kernel.mod_id;
    auto cubin_offset = 0;
    u64 kernel_read_temporal_count = 0;
    u64 kernel_write_temporal_count = 0;
    u64 kernel_read_spatial_count = 0;
    u64 kernel_write_spatial_count = 0;
    u64 kernel_count = 0;
    redshow::SpatialStatistics read_spatial_stats;
    redshow::SpatialStatistics write_spatial_stats;
    redshow::TemporalStatistics read_temporal_stats;
    redshow::TemporalStatistics write_temporal_stats;
    std::vector<redshow::Symbol> &symbols = cubin_map[cubin_id].symbols[mod_id];

    if (analysis_enabled.find(REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) != analysis_enabled.end()) {
      record_data.analysis_type = REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY;
      // read
      record_data.access_type = REDSHOW_ACCESS_READ;
      record_spatial_trace(kernel.read_spatial_trace, kernel.read_pc_count, pc_views_limit,
                           mem_views_limit, record_data, read_spatial_stats,
                           kernel_read_spatial_count);
      // Transform pcs
      transform_data_views(symbols, record_data);
      record_data_callback(cubin_id, kernel_id, &record_data);
      transform_spatial_statistics(cubin_id, symbols, read_spatial_stats);

      // Write
      record_data.access_type = REDSHOW_ACCESS_WRITE;
      record_spatial_trace(kernel.write_spatial_trace, kernel.write_pc_count, pc_views_limit,
                           mem_views_limit, record_data, write_spatial_stats,
                           kernel_write_spatial_count);
      // Transform pcs
      transform_data_views(symbols, record_data);
      record_data_callback(cubin_id, kernel_id, &record_data);
      transform_spatial_statistics(cubin_id, symbols, write_spatial_stats);
    }

    if (analysis_enabled.find(REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) != analysis_enabled.end()) {
      record_data.analysis_type = REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY;
      // Read
      record_data.access_type = REDSHOW_ACCESS_READ;
      record_temporal_trace(kernel.read_pc_pairs, kernel.read_pc_count, pc_views_limit,
                            mem_views_limit, record_data, read_temporal_stats,
                            kernel_read_temporal_count);

      transform_data_views(symbols, record_data);
      record_data_callback(cubin_id, kernel_id, &record_data);
      transform_temporal_statistics(cubin_id, symbols, read_temporal_stats);

      // Write
      record_data.access_type = REDSHOW_ACCESS_WRITE;
      record_temporal_trace(kernel.write_pc_pairs, kernel.write_pc_count, pc_views_limit,
                            mem_views_limit, record_data, write_temporal_stats,
                            kernel_write_temporal_count);

      transform_data_views(symbols, record_data);
      record_data_callback(cubin_id, kernel_id, &record_data);
      transform_temporal_statistics(cubin_id, symbols, write_temporal_stats);
    }

    // Accumulate all access count and red count
    for (auto &iter : kernel.read_pc_count) {
      kernel_count += iter.second;
    }

    for (auto &iter : kernel.write_pc_count) {
      kernel_count += iter.second;
    }

    thread_count += kernel_count;
    thread_read_temporal_count += kernel_read_temporal_count;
    thread_write_temporal_count += kernel_write_temporal_count;
    thread_read_spatial_count += kernel_read_spatial_count;
    thread_write_spatial_count += kernel_write_spatial_count;

    if (mem_views_limit != 0) {
      if (!read_temporal_stats.empty()) {
        redshow::show_temporal_trace(thread_id, kernel_id, kernel_read_temporal_count, kernel_count,
                                     read_temporal_stats, true, false);
      }
      if (!write_temporal_stats.empty()) {
        redshow::show_temporal_trace(thread_id, kernel_id, kernel_write_temporal_count,
                                     kernel_count, write_temporal_stats, false, false);
      }
    }

    if (mem_views_limit != 0) {
      if (!read_spatial_stats.empty()) {
        redshow::show_spatial_trace(thread_id, kernel_id, kernel_read_spatial_count, kernel_count,
                                    read_spatial_stats, true, false);
      }
      if (!write_spatial_stats.empty()) {
        redshow::show_spatial_trace(thread_id, kernel_id, kernel_write_spatial_count, kernel_count,
                                    write_spatial_stats, false, false);
      }
    }
  }

  if (mem_views_limit != 0) {
    if (thread_count != 0) {
      // FIXME(Keren): empty stats for placeholder, should be fixed for better style
      redshow::SpatialStatistics read_spatial_stats;
      redshow::SpatialStatistics write_spatial_stats;
      redshow::TemporalStatistics read_temporal_stats;
      redshow::TemporalStatistics write_temporal_stats;

      redshow::show_temporal_trace(thread_id, 0, thread_read_temporal_count, thread_count,
                                   read_temporal_stats, true, true);
      redshow::show_temporal_trace(thread_id, 0, thread_write_temporal_count, thread_count,
                                   write_temporal_stats, false, true);
      redshow::show_spatial_trace(thread_id, 0, thread_read_spatial_count, thread_count,
                                  read_spatial_stats, true, true);
      redshow::show_spatial_trace(thread_id, 0, thread_write_spatial_count, thread_count,
                                  write_spatial_stats, false, true);
    }
  }

  // Release data
  delete[] record_data.views;
}

void value_flow_flush() {
  // 1. high redundancy nodes
  // 2. duplication graph
  // 3. backpropagate intra-kernel redundancy 
  std::vector<redshow::ValueFlowOp> value_flow_ops;

  for (auto &memset : memsets) {
    redshow::ValueFlowOp op(memset.memory_op_id, memset.memset_id, 0,
                            redshow::VALUE_FLOW_NODE_MEMSET, memset.redundancy, memset.hash);
    value_flow_ops.emplace_back(std::move(op));
  }

  for (auto &memcpy : memcpys) {
    redshow::ValueFlowOp op(memcpy.memcpy_op_id, memcpy.memcpy_id, 0,
                            redshow::VALUE_FLOW_NODE_MEMCPY, memcpy.redundancy, memcpy.hash);
    value_flow_ops.emplace_back(std::move(op));
  }

  for (auto &thread_iter : kernel_map) {
    for (auto &kernel_iter : thread_iter.second) {
      auto kernel_id = kernel_iter.first;
      redshow::ValueFlowOp op(0, kernel_id, 0, redshow::VALUE_FLOW_NODE_KERNEL, 0, "");
      value_flow_ops.emplace_back(std::move(op));
    }
  }

  std::map<int32_t, redshow::ValueFlowRecord> value_flow_records;
  if (redshow::analyze_value_flow(value_flow_graph, value_flow_ops, value_flow_records)) {
    // report result
    redshow::report_value_flow(value_flow_records);
  }
}

redshow_result_t redshow_flush_thread(uint32_t thread_id) {
  PRINT("\nredshow->Enter redshow_flush thread_id %u\n", thread_id);
  kernel_map_lock.lock();

  auto &thread_kernel_map = kernel_map[thread_id];

  kernel_map_lock.unlock();

  if (analysis_enabled.find(REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) != analysis_enabled.end() ||
      analysis_enabled.find(REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) != analysis_enabled.end()) {
    redundancy_flush(thread_id, thread_kernel_map);
  }

  // Remove all kernel records
  kernel_map_lock.lock();

  kernel_map.erase(thread_id);

  kernel_map_lock.unlock();

  return REDSHOW_SUCCESS;
}

redshow_result_t redshow_flush() {
  PRINT("\nredshow->Enter redshow_flush\n");

  // No lock is required for all analysis done in this function because it is only called when a
  // process terminates
  if (analysis_enabled.find(REDSHOW_ANALYSIS_VALUE_FLOW) != analysis_enabled.end()) {
    value_flow_flush();
  }

  return REDSHOW_SUCCESS;
}
