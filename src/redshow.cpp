#include <redshow.h>
#include <instruction.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include "common_lib.h"
#include <cstdlib>

#ifdef DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

#define MIN2(x, y) (x > y ? y : x)

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
  uint64_t memory_id;

  Memory() = default;

  Memory(MemoryRange &memory_range, uint64_t memory_id) :
      memory_range(memory_range), memory_id(memory_id) {}
};

typedef std::map<MemoryRange, Memory> MemoryMap;
static std::map<uint64_t, MemoryMap> memory_snapshot;
static std::mutex memory_snapshot_lock;
static std::map<uint64_t, AccessType> array_type;

struct Kernel {
  uint64_t kernel_id;
  uint32_t cubin_id;
  uint32_t func_index;
  uint64_t func_addr;

  Kernel() = default;

  Kernel(uint64_t kernel_id, uint32_t cubin_id, uint32_t func_index, uint64_t func_addr) :
      kernel_id(kernel_id), cubin_id(cubin_id), func_index(func_index), func_addr(func_addr) {}
};

static std::map<uint64_t, Kernel> kernel_map;
static std::mutex kernel_map_lock;

static std::set<redshow_analysis_type_t> analysis_enabled;

static redshow_log_data_callback_func log_data_callback = NULL;

static __thread uint64_t mini_host_op_id = 0;

extern map<_u64, map<_u64, map<_u64, _u64 >>> hr_trace_map_pc_dist;


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


redshow_result_t trace_analyze(uint32_t cubin_id, uint64_t host_op_id, gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;

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
    // An example to demonstrate how we get information from trace
    size_t size = trace_data->tail_index;
    gpu_patch_record_t *records = reinterpret_cast<gpu_patch_record_t *>(trace_data->records);

    for (size_t i = 0; i < size; ++i) {
      // Iterate over each record
      gpu_patch_record_t *record = records + i;
      Symbol symbol(record->pc);

      auto symbols_iter = std::upper_bound(symbols->begin(), symbols->end(), symbol);

      if (symbols_iter != symbols->begin()) {
        --symbols_iter;
        auto pc_offset = record->pc - symbols_iter->pc;

        if (record->flags & GPU_PATCH_BLOCK_ENTER_FLAG) {
          std::cout << "Enter block: " << record->flat_block_id << std::endl;
        } else if (record->flags & GPU_PATCH_BLOCK_EXIT_FLAG) {
          std::cout << "EXIT block: " << record->flat_block_id << std::endl;
        } else {
          // record->size * 8, byte to bits
          AccessType access_type(0, record->size * 8, AccessType::UNKNOWN);

          if (inst_graph->size() == 0) {
            // Default mode, we identify every data as 32 bits unit size, 32 bits vec size, integer type
            access_type.type = AccessType::INTEGER;
            access_type.unit_size = MIN2(32, access_type.vec_size);
          } else {
            // Accurate mode, when we have instruction information
            auto &inst = inst_graph->node(pc_offset + symbols_iter->cubin_offset);
            std::cout << "Instruction: 0x" << std::hex << pc_offset << std::dec <<
                      " " << inst.op << std::endl;

            AccessType access_type;
            if (record->flags & GPU_PATCH_READ) {
              access_type = load_data_type(inst.pc, *inst_graph);
            } else if (record->flags & GPU_PATCH_WRITE) {
              access_type = store_data_type(inst.pc, *inst_graph);
            }

            // Fall back to default mode if failed
            if (access_type.type == AccessType::UNKNOWN) {
              access_type.type = AccessType::INTEGER;
              access_type.unit_size = MIN2(32, access_type.vec_size);
            }
          }

          std::cout << "Access type: " << access_type.to_string() << std::endl;

          for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
            if (record->active & (0x1 << j)) {
              std::cout << "Address: 0x" << std::hex << record->address[j] << std::dec << std::endl;
              MemoryRange memory_range(record->address[j], record->address[j]);
              auto iter = memory_map->upper_bound(memory_range);
              if (iter != memory_map->begin()) {
                --iter;
                std::cout << "Memory ID: " << iter->second.memory_id << std::endl;
              }

//                Value part
                std::cout << "Value: 0x" << std::hex;
                for (size_t k = 0; k < GPU_PATCH_MAX_ACCESS_SIZE; ++k) {
                  unsigned int c = record->value[j][k];
                  std::cout << c;
                }
                std::cout << std::dec << std::endl;

                for (size_t m = 0; m < access_type.vec_size / access_type.unit_size; m++) {
                  _u64 value = 0;
                  memcpy(&value, &record->value[j][m * access_type.unit_size], access_type.unit_size);
                  get_hr_trace_map(pc_offset, value, (_u64) iter->second.memory_id, hr_trace_map_pc_dist);
                }


              }


            }
          }
        }
      } else {
        std::cout << "PC: 0x" << std::hex << record->pc << " not found" << std::endl;
      }
    }
#endif
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

#ifdef DEBUG
    for (auto &symbol : symbols) {
      std::cout << "Symbol index: " << symbol.index << std::endl;
      std::cout << "Symbol cubin offset: 0x" << std::hex << symbol.cubin_offset << std::dec << std::endl;
      std::cout << "Symbol pc: 0x" << std::hex << symbol.pc << std::dec << std::endl;
    }
#endif

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
  auto iter = memory_snapshot.lower_bound(host_op_id);
  if (iter != memory_snapshot.begin()) {
    --iter;
    // Take a snapshot
    memory_map = iter->second;
    if (memory_map.find(memory_range) == memory_map.end()) {
      memory_map[memory_range].memory_range = memory_range;
      memory_map[memory_range].memory_id = memory_id;
      memory_snapshot[host_op_id] = memory_map;
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_DUPLICATE_ENTRY;
    }
  } else if (memory_snapshot.size() == 0) {
    // First snapshot
    memory_map[memory_range].memory_range = memory_range;
    memory_map[memory_range].memory_id = memory_id;
    memory_snapshot[host_op_id] = memory_map;
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
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
  auto snapshot_iter = memory_snapshot.lower_bound(host_op_id);
  if (snapshot_iter != memory_snapshot.begin()) {
    --snapshot_iter;
    // Take a snapshot
    memory_map = snapshot_iter->second;
    auto memory_map_iter = memory_map.find(memory_range);
    if (memory_map_iter != memory_map.end()) {
      memory_map.erase(memory_map_iter);
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


redshow_result_t redshow_analyze(uint32_t cubin_id, uint64_t kernel_id, uint64_t host_op_id,
                                 gpu_patch_buffer_t *trace_data) {
  PRINT("\nredshow->Enter redshow_analyze\ncubin_id: %u\nkernel_id: %p\nhost_op_id: %lu\ntrace_data: %p\n",
        cubin_id, kernel_id, host_op_id, trace_data);

  redshow_result_t result;

  // Analyze trace_data
  result = trace_analyze(cubin_id, host_op_id, trace_data);

  if (result == REDSHOW_SUCCESS) {
    // Store trace_data
    kernel_map_lock.lock();
    if (kernel_map.find(kernel_id) != kernel_map.end()) {
      // Merge existing data
    } else {
      // Allocate new record data
      kernel_map[kernel_id].kernel_id = kernel_id;
      kernel_map[kernel_id].cubin_id = cubin_id;
    }
    kernel_map_lock.unlock();

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
    for (auto &iter : memory_snapshot) {
      if (iter.first < mini_host_op_id) {
        ids.push_back(iter.first);
      }
    }
    for (auto &id : ids) {
      memory_snapshot.erase(id);
    }
    memory_snapshot_lock.unlock();

    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
  }

  return result;
}
