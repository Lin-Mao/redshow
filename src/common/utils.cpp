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
  auto *dst_ptr = reinterpret_cast<unsigned char *>(dst);
  auto *src_ptr = reinterpret_cast<unsigned char *>(src);

#ifdef OPENMP
  #pragma omp parallel for if (len > OMP_SEQ_LEN)
#endif
  for (size_t i = 0; i < len; ++i) {
    dst_ptr[i] = src_ptr[i];
  }
}

}
