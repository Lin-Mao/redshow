#include "operation/memcpy.h"

#include <string>

#include "common/hash.h"
#include "common/utils.h"

namespace redshow {

double compute_memcpy_redundancy(u64 dst_start, u64 src_start, u64 len) {
  // compare every byte
  double same = 0;

  auto *dst_ptr = reinterpret_cast<unsigned char *>(dst_start);
  auto *src_ptr = reinterpret_cast<unsigned char *>(src_start);

  for (size_t i = 0; i < len; ++i) {
    if (*dst_ptr == *src_ptr) {
      same += 1.0;
    }
  }

  return same / len;
}

}  // namespace redshow