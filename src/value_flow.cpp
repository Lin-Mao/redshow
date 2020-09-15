#include "value_flow.h"

#include <string>

#include "utils.h"
#include "hash.h"

namespace redshow {

namespace value_flow {

std::string compute_memory_hash(uint64_t start, uint64_t len) {
  // Use sha256
  return hash::sha256(reinterpret_cast<void *>(start), len);
}

double compute_memory_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len) {
  // compare every byte
  double same = 0;

  auto *dst_ptr = reinterpret_cast<unsigned char *>(dst_start);
  auto *src_ptr = reinterpret_cast<unsigned char *>(src_start);

  for (size_t i = 0; i < len; ++i) {
    if (dst_ptr == src_ptr) {
      same += 1.0;
    }
  }

  return same / len;
}

}  // namespace value_flow

}  // namespace redshow