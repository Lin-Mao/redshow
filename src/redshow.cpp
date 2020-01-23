#include <redshow.h>
#include <instruction_stat.h>

#include <mutex>
#include <map>
#include <set>
#include <string>

#include <cstdlib>

/*
 * Global data structures
 */

struct Cubin {
  uint32_t cubin_id;
  std::string path;
  std::vector<InstructionStat> inst_stats;

  Cubin() = default;
  Cubin(uint32_t cubin_id, const char *path_,
        std::vector<InstructionStat> &inst_stats) : 
        cubin_id(cubin_id), path(path_), inst_stats(inst_stats) {}
};

std::map<uint32_t, Cubin> cubin_map;
std::mutex cubin_map_lock;


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
  uint64_t memory_id;
  MemoryRange memory_range;

  Memory() = default;
  Memory(uint64_t memory_id, uint64_t start, uint64_t end) :
    memory_id(memory_id), memory_range(start, end) {}
};

std::map<uint64_t, Memory> memory_map;
std::mutex memory_map_lock;


struct Kernel {
  uint64_t kernel_id;
  uint32_t cubin_id;
  uint32_t func_index;
  uint64_t func_addr;

  Kernel() = default;
  Kernel(uint64_t kernel_id, uint32_t cubin_id, uint32_t func_index, uint64_t func_addr) :
    kernel_id(kernel_id), cubin_id(cubin_id), func_index(func_index), func_addr(func_addr) {}
};

std::map<uint64_t, Kernel> kernel_map;
std::mutex kernel_map_lock;

std::set<redshow_analysis_type_t> analysis_enabled;

redshow_log_data_callback_func log_data_callback = NULL;

/*
 * Static methods
 */
bool cmd_exec(const std::string &cmd) {
  return system(cmd.c_str());
}


redshow_result_t cubin_analyze(const char *path, std::vector<InstructionStat> &inst_stats) {
  redshow_result_t result = REDSHOW_SUCCESS;

  std::string cubin_path = std::string(path);
  auto iter = cubin_path.rfind("/");
  if (iter == std::string::npos) {
    result = REDSHOW_ERROR_NO_SUCH_FILE;
  } else {
    // x/x.cubin
    // 012345678
    std::string cubin_name = cubin_path.substr(iter + 1, cubin_path.size() - iter);
    cmd_exec("mkdir -p redshow/cubins");
    cmd_exec("cp " + cubin_path + " redshow/cubins/" + cubin_name);
    if (cmd_exec("hpcstruct redshow/cubins/" + cubin_name) != 0) {
      result = REDSHOW_ERROR_NO_SUCH_FILE;
    } else {
      if (read_instruction_stats("nvidia/" + cubin_name + ".inst", inst_stats)) {
        result = REDSHOW_SUCCESS;
      } else {
        result = REDSHOW_ERROR_FAILED_ANALYZE_CUBIN;
      }
    }
  }
  
  return result;
}


redshow_result_t trace_analyze(gpu_patch_buffer_t *trace_data) {
  return REDSHOW_SUCCESS;
}

/*
 * Interface methods
 */

redshow_result_t redshow_analysis_output(const char *path) {
  return REDSHOW_SUCCESS;
};


redshow_result_t redshow_analysis_enable(redshow_analysis_type_t analysis_type) {
  analysis_enabled.insert(analysis_type);
  return REDSHOW_SUCCESS;
}


redshow_result_t redshow_analysis_disable(redshow_analysis_type_t analysis_type) {
  analysis_enabled.erase(analysis_type);
  return REDSHOW_SUCCESS;
}


redshow_result_t redshow_cubin_register(uint32_t cubin_id, const char *path) {
  redshow_result_t result;

  std::vector<InstructionStat> inst_stats;
  result = cubin_analyze(path, inst_stats);

  if (result == REDSHOW_SUCCESS) {
    cubin_map_lock.lock();
    if (cubin_map.find(cubin_id) == cubin_map.end()) {
      cubin_map[cubin_id].cubin_id = cubin_id;
      cubin_map[cubin_id].path = path;
      cubin_map[cubin_id].inst_stats = inst_stats;
    } else {
      result = REDSHOW_ERROR_DUPLICATE_ENTRY;
    }
    cubin_map_lock.unlock();
  }

  return result;
}


redshow_result_t redshow_cubin_unregister(uint32_t cubin_id) {
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


redshow_result_t redshow_memory_register(uint64_t memory_id, uint64_t start, uint64_t end) {
  redshow_result_t result;

  memory_map_lock.lock();
  if (memory_map.find(memory_id) == memory_map.end()) {
    memory_map[memory_id].memory_id = memory_id;
    memory_map[memory_id].memory_range.start = start;
    memory_map[memory_id].memory_range.end = end;
    result = REDSHOW_SUCCESS;
  } else {
    result = REDSHOW_ERROR_DUPLICATE_ENTRY;
  }
  memory_map_lock.unlock();

  return result;
}


redshow_result_t redshow_memory_unregister(uint64_t memory_id) {
  redshow_result_t result;

  memory_map_lock.lock();
  if (memory_map.find(memory_id) != memory_map.end()) {
    memory_map.erase(memory_id);
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

redshow_result_t redshow_analyze(uint32_t cubin_id, uint32_t func_index,
  uint64_t func_addr, uint64_t kernel_id, gpu_patch_buffer_t *trace_data) {
  redshow_result_t result;

  // Analyze trace_data
  result = trace_analyze(trace_data);

  if (result == REDSHOW_SUCCESS) {
    // Store trace_data
    kernel_map_lock.lock();
    if (kernel_map.find(kernel_id) != kernel_map.end()) {
      // Merge existing data
    } else {
      // Allocate new record data
      kernel_map[kernel_id].kernel_id = kernel_id;
      kernel_map[kernel_id].cubin_id = cubin_id;
      kernel_map[kernel_id].func_index = func_index;
      kernel_map[kernel_id].func_addr = func_addr;
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
