#include "operation/memset.h"

#include <string>

#include "common/hash.h"
#include "common/utils.h"

namespace redshow {

u64 compute_memset_redundancy(u64 start, u32 value, u64 len) {
  // compare every byte
  u64 same = 0;

  auto *ptr = reinterpret_cast<unsigned char *>(start);

  for (size_t i = 0; i < len; ++i) {
    if (ptr[i] == static_cast<unsigned char>(value)) {
      same += 1;
    }
  }

  return same;
}

}  // namespace redshow
