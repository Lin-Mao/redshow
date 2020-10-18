#include "memory.h"

#include <string>

#include "common/hash.h"
#include "common/utils.h"

namespace redshow {

std::string compute_memory_hash(u64 start, u64 len) {
  return sha256(reinterpret_cast<void *>(start), len);
}

}