#include <redshow.h>
#include <instruction.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <map>
#include <string>
#include <iostream>
#include <fstream>

#include <cstdlib>

#include "common_lib.h"
#include "utils.h"

#ifdef DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

#define MIN2(x, y) (x > y ? y : x)
#define MAX2(x, y) (x > y ? x : y)

/*
 * Global data structures
 */

struct Cubin {
  uint32_t cubin_id;
  std::string path;
  std::vector<Symbol> symbols;
  InstructionGraph inst_graph;

  Cubin() = default;

  Cubin(uint32_t cubin_id, const char *path_,
        InstructionGraph &inst_graph) :
      cubin_id(cubin_id), path(path_), inst_graph(inst_graph) {}
};

static std::map<uint32_t, Cubin> cubin_map;
static std::mutex cubin_map_lock;


struct MemoryRange {
  uint64_t start;
  uint64_t end;

  MemoryRange() = default;

  MemoryRange(uint64_t start, uint64_t end) : start(start), end(end) {}

  bool operator<(const MemoryRange &other) const {
    return start < other.start;
  }
};

struct Memory {
  MemoryRange memory_range;
  uint64_t memory_op_id;
  uint64_t memory_id;

  Memory() = default;

  Memory(MemoryRange &memory_range, uint64_t memory_op_id, uint64_t memory_id) :
      memory_range(memory_range), memory_op_id(memory_op_id), memory_id(memory_id) {}
};

typedef std::map<MemoryRange, Memory> MemoryMap;
static std::map<uint64_t, MemoryMap> memory_snapshot;
static std::mutex memory_snapshot_lock;

struct Kernel {
  uint64_t kernel_id;
  uint32_t cubin_id;
  uint32_t func_index;
  uint64_t func_addr;

  SpatialTrace read_spatial_trace;
  SpatialTrace write_spatial_trace;

  TemporalTrace read_temporal_trace;
  PCPairs read_pc_pairs;

  TemporalTrace write_temporal_trace;
  PCPairs write_pc_pairs;

  Kernel() = default;

  Kernel(uint64_t kernel_id, uint32_t cubin_id, uint32_t func_index, uint64_t func_addr) :
      kernel_id(kernel_id), cubin_id(cubin_id), func_index(func_index), func_addr(func_addr) {}
};

static std::map<uint32_t, std::map<uint64_t, Kernel> > kernel_map;
static std::mutex kernel_map_lock;

static std::set<redshow_analysis_type_t> analysis_enabled;

static redshow_log_data_callback_func log_data_callback = NULL;

static redshow_record_data_callback_func record_data_callback = NULL;

static __thread uint64_t mini_host_op_id = 0;

static uint32_t num_views_limit = 0;

enum {
  MEMORY_ID_SHARED = 0,
  MEMORY_ID_LOCAL = 1
};


redshow_result_t cubin_analyze(const char *path, std::vector<Symbol> &symbols, InstructionGraph &inst_graph) {
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


redshow_result_t transform_pc(std::vector<Symbol> &symbols, uint64_t pc,
  uint32_t &function_index, uint64_t &cubin_offset, uint64_t &pc_offset) {
  redshow_result_t result = REDSHOW_SUCCESS;

  Symbol symbol(pc);

  auto symbols_iter = std::upper_bound(symbols.begin(), symbols.end(), symbol);

  if (symbols_iter != symbols.begin()) {
    --symbols_iter;
    pc_offset = pc - symbols_iter->pc;
    cubin_offset = pc_offset + symbols_iter->cubin_offset;
    function_index = symbols_iter->index;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }

  return result;
}


redshow_result_t trace_analyze(Kernel &kernel, uint64_t host_op_id, gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

  auto cubin_id = kernel.cubin_id;
  auto &read_spatial_trace = kernel.read_spatial_trace;
  auto &write_spatial_trace = kernel.write_spatial_trace;
  auto &read_temporal_trace = kernel.read_temporal_trace;
  auto &read_pc_pairs = kernel.read_pc_pairs;
  auto &write_temporal_trace = kernel.write_temporal_trace;
  auto &write_pc_pairs = kernel.write_pc_pairs;

  std::vector<Symbol> *symbols = NULL;
  InstructionGraph *inst_graph = NULL;
  // Cubin path is added just for debugging purpose
  std::string cubin_path;

  cubin_map_lock.lock();
  if (cubin_map.find(cubin_id) == cubin_map.end()) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    symbols = &(cubin_map[cubin_id].symbols);
    inst_graph = &(cubin_map[cubin_id].inst_graph);
    cubin_path = cubin_map[cubin_id].path;
  }
  cubin_map_lock.unlock();

  // Cubin not found
  if (result == REDSHOW_ERROR_NOT_EXIST_ENTRY) {
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
  if (result == REDSHOW_ERROR_NOT_EXIST_ENTRY) {
    return result;
  }

  if (result == REDSHOW_SUCCESS) {
    size_t size = trace_data->head_index;
    gpu_patch_record_t *records = reinterpret_cast<gpu_patch_record_t *>(trace_data->records);

    for (size_t i = 0; i < size; ++i) {
      // Iterate over each record
      gpu_patch_record_t *record = records + i;
      ThreadId thread_id{record->flat_block_id, record->flat_thread_id};

      if (record->size == 0) {
        // Fast path, no thread active
        continue;
      }

      if (record->flags & GPU_PATCH_BLOCK_ENTER_FLAG) {
        // Skip analysis  
      } else if (record->flags & GPU_PATCH_BLOCK_EXIT_FLAG) {
        // Remove temporal records
        read_temporal_trace.erase(thread_id);
        write_temporal_trace.erase(thread_id);
      } else {
        uint32_t function_index = 0;
        uint64_t cubin_offset = 0;
        uint64_t pc_offset = 0;
        transform_pc(*symbols, record->pc, function_index, cubin_offset, pc_offset);

        // record->size * 8, byte to bits
        AccessType access_type;

        if (inst_graph->size() != 0) {
          // Accurate mode, when we have instruction information
          auto &inst = inst_graph->node(cubin_offset);

          if (record->flags & GPU_PATCH_READ) {
            access_type = load_data_type(inst.pc, *inst_graph);
          } else if (record->flags & GPU_PATCH_WRITE) {
            access_type = store_data_type(inst.pc, *inst_graph);
          }
          // Fall back to default mode if failed
        }

        if (access_type.type == AccessType::UNKNOWN) {
          // Default mode, we identify every data as 32 bits unit size, 32 bits vec size, float type
          access_type.type = AccessType::FLOAT;
          access_type.vec_size = record->size * 8;
          access_type.unit_size = MIN2(GPU_PATCH_WARP_SIZE, access_type.vec_size * 8);
        }

        // TODO: accelerate by handling all threads in a warp together
        for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
          if (record->active & (0x1u << j)) {
            MemoryRange memory_range(record->address[j], record->address[j]);
            auto iter = memory_map->upper_bound(memory_range);
            uint64_t memory_op_id = 0;
            if (iter != memory_map->begin()) {
              --iter;
              memory_op_id = iter->second.memory_op_id;
            }

            if (memory_op_id == 0) {
              // It means the memory is local, shared, or allocated in an unknown way
              if (record->flags & GPU_PATCH_LOCAL) {
                memory_op_id = MEMORY_ID_LOCAL; 
              } else if (record->flags & GPU_PATCH_SHARED) {
                memory_op_id = MEMORY_ID_SHARED;
              } else {
                // Unknown allocation
              }
            }

            if (memory_op_id != 0) {
              for (size_t m = 0; m < access_type.vec_size / access_type.unit_size; m++) {
                uint64_t value = 0;
                memcpy(&value, &record->value[j][m * access_type.unit_size], access_type.unit_size >> 3u);

                for (auto analysis : analysis_enabled) {
                  if (analysis == REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) {
                    if (record->flags & GPU_PATCH_READ) {
                      get_spatial_trace(record->pc, value, memory_op_id, access_type.type, read_spatial_trace);
                    } else {
                      get_spatial_trace(record->pc, value, memory_op_id, access_type.type, write_spatial_trace);
                    }
                  } else if (analysis == REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) {
                    if (record->flags & GPU_PATCH_READ) {
                      get_temporal_trace(record->pc, thread_id, record->address[j], value, access_type.type,
                        read_temporal_trace, read_pc_pairs);
                    } else {
                      get_temporal_trace(record->pc, thread_id, record->address[j], value, access_type.type,
                        write_temporal_trace, write_pc_pairs);
                    }
                  } else {
                    // Pass
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return result;
}

/*
 * Interface methods
 */

redshow_result_t redshow_analysis_output(const char *path) {
  PRINT("\nredshow->Enter redshow_analysis_output\npath: %s\n", path);

  return REDSHOW_SUCCESS;
};


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


redshow_result_t redshow_cubin_register(uint32_t cubin_id, uint32_t nsymbols, uint64_t *symbol_pcs, const char *path) {
  PRINT("\nredshow->Enter redshow_cubin_register\ncubin_id: %u\npath: %s\n", cubin_id, path);

  redshow_result_t result;

  InstructionGraph inst_graph;
  std::vector<Symbol> symbols(nsymbols);
  result = cubin_analyze(path, symbols, inst_graph);

  if (result == REDSHOW_SUCCESS || result == REDSHOW_ERROR_NO_SUCH_FILE) {
    // It is ok to not have inst files, in such a case we just record symbol offset and use the default access type
    // The problem can be eliminated once we have the next nvdisasm and dyninst ready
    // Assign symbol pc
    if (result == REDSHOW_SUCCESS) {
      for (auto &symbol : symbols) {
        symbol.pc = symbol_pcs[symbol.index];
      }
    } else {
      for (auto i = 0; i < nsymbols; ++i) {
        symbols[i].pc = symbol_pcs[i];
      }
    }

    // Sort symbols by pc
    std::sort(symbols.begin(), symbols.end());

    cubin_map_lock.lock();
    if (cubin_map.find(cubin_id) == cubin_map.end()) {
      cubin_map[cubin_id].cubin_id = cubin_id;
      cubin_map[cubin_id].path = path;
      cubin_map[cubin_id].inst_graph = inst_graph;
      cubin_map[cubin_id].symbols = symbols;
    } else {
      result = REDSHOW_ERROR_DUPLICATE_ENTRY;
    }
    cubin_map_lock.unlock();
  }

  return result;
}


redshow_result_t redshow_cubin_unregister(uint32_t cubin_id) {
  PRINT("\nredshow->Enter redshow_cubin_unregister\ncubin_id: %u\n", cubin_id);

  redshow_result_t result;

  cubin_map_lock.lock();
  if (cubin_map.find(cubin_id) != cubin_map.end()) {
    cubin_map.erase(cubin_id);
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  cubin_map_lock.unlock();

  return result;
}


redshow_result_t redshow_memory_register(uint64_t start, uint64_t end, uint64_t host_op_id, uint64_t memory_id) {
  PRINT("\nredshow->Enter redshow_memory_register\nstart: %p\nend: %p\nmemory_id: %lu\n", start, end, memory_id);

  redshow_result_t result;
  MemoryMap memory_map;
  MemoryRange memory_range(start, end);

  memory_snapshot_lock.lock();
  if (memory_snapshot.size() == 0) {
    // First snapshot
    memory_map[memory_range].memory_range = memory_range;
    memory_map[memory_range].memory_id = memory_id;
    memory_map[memory_range].memory_op_id = host_op_id;
    memory_snapshot[host_op_id] = memory_map;
    result = REDSHOW_SUCCESS;
    PRINT("First host_op_id %lu registered\n", host_op_id);
  } else { 
    auto iter = memory_snapshot.upper_bound(host_op_id);
    if (iter != memory_snapshot.begin()) {
      --iter;
      // Take a snapshot
      memory_map = iter->second;
      if (memory_map.find(memory_range) == memory_map.end()) {
        memory_map[memory_range].memory_range = memory_range;
        memory_map[memory_range].memory_id = memory_id;
        memory_map[memory_range].memory_op_id = host_op_id;
        memory_snapshot[host_op_id] = memory_map;
        result = REDSHOW_SUCCESS;
        PRINT("host_op_id %lu registered\n", host_op_id);
      } else {
        result = REDSHOW_ERROR_DUPLICATE_ENTRY;
      }
    } else {
      result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
    }
  }
  memory_snapshot_lock.unlock();

  return result;
}


redshow_result_t redshow_memory_unregister(uint64_t start, uint64_t end, uint64_t host_op_id) {
  PRINT("\nredshow->Enter redshow_memory_unregister\nstart: %p\nend: %p\n", start, end);

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


redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func) {
  log_data_callback = func;
}


redshow_result_t redshow_record_data_callback_register(redshow_record_data_callback_func func, uint32_t limit) {
  record_data_callback = func;
  num_views_limit = limit;
}


redshow_result_t redshow_analyze(uint32_t thread_id, uint32_t cubin_id, uint64_t kernel_id, uint64_t host_op_id, gpu_patch_buffer_t *trace_data) {
  PRINT("\nredshow->Enter redshow_analyze\ncubin_id: %u\nkernel_id: %p\nhost_op_id: %lu\ntrace_data: %p\n",
        cubin_id, kernel_id, host_op_id, trace_data);

  redshow_result_t result;

  kernel_map_lock.lock();

  auto &thread_kernel_map = kernel_map[thread_id];

  kernel_map_lock.unlock();

  // Analyze trace_data
  Kernel &kernel = thread_kernel_map[kernel_id];
  kernel.kernel_id = kernel_id;
  kernel.cubin_id = cubin_id;
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


redshow_result_t redshow_flush(uint32_t thread_id) {
  PRINT("\nredshow->Enter redshow_flush thread_id %u\n", thread_id);

  redshow_record_data_t record_data;

  kernel_map_lock.lock();

  auto &thread_kernel_map = kernel_map[thread_id];

  kernel_map_lock.unlock();

  record_data.views = new redshow_record_view_t[num_views_limit];

  for (auto &kernel_iter : thread_kernel_map) {
    auto kernel_id = kernel_iter.first;
    auto &kernel = kernel_iter.second;
    auto cubin_id = kernel.cubin_id;
    auto cubin_offset = 0;

    std::vector<Symbol> *symbols = &(cubin_map[cubin_id].symbols);

    for (auto analysis : analysis_enabled) {
      if (analysis == REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY) {
        record_data.analysis_type = REDSHOW_ANALYSIS_SPATIAL_REDUNDANCY;
        // Read
        record_data.access_type = REDSHOW_ACCESS_READ;
        record_spatial_trace(kernel.read_spatial_trace, record_data, num_views_limit);
        // Transform pcs
        for (auto i = 0; i < record_data.num_views; ++i) {
          uint64_t pc = record_data.views[i].pc_offset;
          uint32_t function_index = 0;
          uint64_t cubin_offset = 0;
          uint64_t pc_offset = 0;
          transform_pc(*symbols, pc, function_index, cubin_offset, pc_offset);
          record_data.views[i].function_index = function_index;
          record_data.views[i].pc_offset = pc_offset;
        }
        record_data_callback(cubin_id, kernel_id, &record_data);
        // Write
        record_data.access_type = REDSHOW_ACCESS_WRITE;
        record_spatial_trace(kernel.write_spatial_trace, record_data, num_views_limit);
        // Transform pcs
        for (auto i = 0; i < record_data.num_views; ++i) {
          uint64_t pc = record_data.views[i].pc_offset;
          uint32_t function_index = 0;
          uint64_t cubin_offset = 0;
          uint64_t pc_offset = 0;
          transform_pc(*symbols, pc, function_index, cubin_offset, pc_offset);
          record_data.views[i].function_index = function_index;
          record_data.views[i].pc_offset = pc_offset;
        }
        record_data_callback(cubin_id, kernel_id, &record_data);
      } else if (analysis == REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY) {
        record_data.analysis_type = REDSHOW_ANALYSIS_TEMPORAL_REDUNDANCY;
        // Read
        record_data.access_type = REDSHOW_ACCESS_READ;
        record_temporal_trace(kernel.read_pc_pairs, record_data, num_views_limit);
        // Transform pcs
        for (auto i = 0; i < record_data.num_views; ++i) {
          uint64_t pc = record_data.views[i].pc_offset;
          uint32_t function_index = 0;
          uint64_t cubin_offset = 0;
          uint64_t pc_offset = 0;
          transform_pc(*symbols, pc, function_index, cubin_offset, pc_offset);
          record_data.views[i].function_index = function_index;
          record_data.views[i].pc_offset = pc_offset;
        }
        record_data_callback(cubin_id, kernel_id, &record_data);
        // Write
        record_data.access_type = REDSHOW_ACCESS_WRITE;
        record_temporal_trace(kernel.write_pc_pairs, record_data, num_views_limit);
        // Transform pcs
        for (auto i = 0; i < record_data.num_views; ++i) {
          uint64_t pc = record_data.views[i].pc_offset;
          uint32_t function_index = 0;
          uint64_t cubin_offset = 0;
          uint64_t pc_offset = 0;
          transform_pc(*symbols, pc, function_index, cubin_offset, pc_offset);
          record_data.views[i].function_index = function_index;
          record_data.views[i].pc_offset = pc_offset;
        }
        record_data_callback(cubin_id, kernel_id, &record_data);
      }
    }
  }

  // Remove all kernel records
  kernel_map_lock.lock();

  kernel_map.erase(thread_id);

  kernel_map_lock.unlock();

  // Release data
  delete [] record_data.views;
}
