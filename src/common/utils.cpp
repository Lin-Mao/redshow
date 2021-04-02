#include "common/utils.h"

#include <cstring>

namespace redshow {

u64 value_to_double(u64 a, int decimal_degree_f64) {
  u64 c = a;
  u64 bits = 52 - decimal_degree_f64;
  u64 mask = 0xffffffffffffffff << bits;
  c = c & mask;
  return c;
}

u64 value_to_float(u64 a, int decimal_degree_f32) {
  u32 c = a & 0xffffffffu;
  u64 bits = 23 - decimal_degree_f32;
  u64 mask = 0xffffffffffffffff << bits;
  c &= mask;
  u64 b = 0;
  memcpy(&b, &c, sizeof(c));
  return b;
}

void memory_copy(void *dst, void *src, size_t len) {
#ifdef OPENMP
  auto *dst_ptr = reinterpret_cast<unsigned char *>(dst);
  auto *src_ptr = reinterpret_cast<unsigned char *>(src);

// neseted parallelism is fine
  if (len > OMP_SEQ_LEN) {
#pragma omp parallel for simd
    for (size_t i = 0; i < len; ++i) {
      dst_ptr[i] = src_ptr[i];
    }
  } else {
    memcpy(dst, src, len);
  }
#else
  memcpy(dst, src, len);
#endif
}

}
