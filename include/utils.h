#ifndef REDSHOW_UTILS_H
#define REDSHOW_UTILS_H

#include <tuple>
#include <string>

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

// We will change the bits after this index to 0. F32:23, F64:52
const int VALID_FLOAT_DIGITS = 23;
const int VALID_DOUBLE_DIGITS = 52;

#endif 
