#include <redshow.h>
#include <instruction.h>

#include <algorithm>
#include <limits>
#include <mutex>
#include <map>
#include <string>
#include <iostream>

#include <cstdlib>

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

  bool operator < (const MemoryRange &other) const {
    if (start < other.start) {
      return true;
    } else if (start > other.start) {
      return false;
    } else {
      return end < other.end;
    } 
  }
};

struct Memory {
  MemoryRange memory_range;
  uint64_t memory_id;

  Memory() = default;
  Memory(MemoryRange &memory_range, uint64_t memory_id) :
    memory_range(memory_range), memory_id(memory_id) {}
};

static std::map<MemoryRange, Memory> memory_map;
static std::mutex memory_map_lock;


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
    std::cout << dir_name << std::endl;
    // instructions are analyzed before hpcrun
    if (parse_instructions(dir_name + "/structs/nvidia/" + cubin_name + ".inst", symbols, inst_graph)) {
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
    }
  }
  
  return result;
}


redshow_result_t trace_analyze(uint32_t cubin_id, gpu_patch_buffer_t *trace_data) {
  redshow_result_t result = REDSHOW_SUCCESS;
  
  std::vector<Symbol> *symbols = NULL;
  InstructionGraph *inst_graph = NULL; 
  
  cubin_map_lock.lock();
  if (cubin_map.find(cubin_id) == cubin_map.end()) {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  } else {
    symbols = &(cubin_map[cubin_id].symbols);
    inst_graph = &(cubin_map[cubin_id].inst_graph);
  }
  cubin_map_lock.unlock();

  if (result == REDSHOW_SUCCESS) {
#ifdef DEBUG
    // An example to demonstrate how we get information from trace
    size_t size = trace_data->tail_index;
    gpu_patch_record_t *records = reinterpret_cast<gpu_patch_record_t *>(trace_data->records);

    for (size_t i = 0; i < size; ++i) {
      // Iterate over each record
      gpu_patch_record_t *record = records + i;
      Symbol symbol(record->pc);
     
      auto iter = std::upper_bound(symbols->begin(), symbols->end(), symbol);

      if (iter != symbols->end()) {
        auto pc_offset = record->pc - iter->pc;

        if (record->flags & GPU_PATCH_BLOCK_ENTER_FLAG) {
          std::cout << "Enter block: " << record->flat_block_id << std::endl;
        } else if (record->flags & GPU_PATCH_BLOCK_EXIT_FLAG) {
          std::cout << "EXIT block: " << record->flat_block_id << std::endl;
        } else {
          auto &inst = inst_graph->node(pc_offset + iter->cubin_offset);
          std::cout << "Instruction: 0x" << std::hex << pc_offset << std::dec <<
            " " << inst.op << std::endl;

          for (size_t j = 0; j < GPU_PATCH_WARP_SIZE; ++j) {
            if (record->active & (0x1 << j)) {
              std::cout << "Address: 0x" << std::hex << record->address[j] << std::dec << std::endl;
              std::cout << "Value: " << record->value[j] << std::endl;
            }
          }
        }
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

  if (result == REDSHOW_SUCCESS) {
    // Assign symbol pc
    for (auto &symbol : symbols) {
      symbol.pc = symbol_pcs[symbol.index];
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


redshow_result_t redshow_memory_register(uint64_t start, uint64_t end, uint64_t memory_id) {
  PRINT("\nredshow->Enter redshow_memory_register\nstart: %p\nend: %p\nmemory_id: %lu\n", start, end, memory_id);

  redshow_result_t result;
  MemoryRange memory_range(start, end);

  memory_map_lock.lock();
  if (memory_map.find(memory_range) == memory_map.end()) {
    memory_map[memory_range].memory_range = memory_range;
    memory_map[memory_range].memory_id = memory_id;
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_DUPLICATE_ENTRY;
  }
  memory_map_lock.unlock();

  return result;
}


redshow_result_t redshow_memory_unregister(uint64_t start, uint64_t end) {
  PRINT("\nredshow->Enter redshow_memory_unregister\nstart: %p\nend: %p\n", start, end);

  redshow_result_t result;
  MemoryRange memory_range(start, end);

  memory_map_lock.lock();
  if (memory_map.find(memory_range) != memory_map.end()) {
    memory_map.erase(memory_range);
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_NOT_EXIST_ENTRY;
  }
  memory_map_lock.unlock();

  return result;
}


redshow_result_t redshow_log_data_callback_register(redshow_log_data_callback_func func) {
  log_data_callback = func;
}


redshow_result_t redshow_analyze(uint32_t cubin_id, uint64_t kernel_id, gpu_patch_buffer_t *trace_data) {
  PRINT("\nredshow->Enter redshow_analyze\ncubin_id: %u\nkernel_id: %p\ntrace_data: %p\n",
    cubin_id, kernel_id, trace_data);

  redshow_result_t result;

  // Analyze trace_data
  result = trace_analyze(cubin_id, trace_data);

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
      result = REDSHOW_SUCCESS;
    } else {
      result = REDSHOW_ERROR_NOT_REGISTER_CALLBACK;
    }
  }

  return result;
}
