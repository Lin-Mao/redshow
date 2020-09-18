#include "utils.h"

#include <cstring>

u64 store_to_double(u64 a, int decimal_degree_f64) {
  u64 c = a;
  u64 bits = 52 - decimal_degree_f64;
  u64 mask = 0xffffffffffffffff << bits;
  c = c & mask;
  return c;
}

u64 store_to_float(u64 a, int decimal_degree_f32) {
  u32 c = a & 0xffffffffu;
  u64 bits = 23 - decimal_degree_f32;
  u64 mask = 0xffffffffffffffff << bits;
  c &= mask;
  u64 b = 0;
  memcpy(&b, &c, sizeof(c));
  return b;
}
