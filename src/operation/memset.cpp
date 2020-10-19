#include "operation/memset.h"

#include <string>

#include "common/hash.h"
#include "common/utils.h"

namespace redshow {

double compute_memset_redundancy(u64 start, u32 value, u64 len) {
  // compare every byte
  double same = 0;

  auto *ptr = reinterpret_cast<unsigned char *>(start);

  for (size_t i = 0; i < len; ++i) {
    if (*ptr == static_cast<unsigned char>(value)) {
      same += 1.0;
    }
  }

  return same / len;
}

}  // namespace redshow