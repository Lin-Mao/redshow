#ifndef REDSHOW_UTILS_H
#define REDSHOW_UTILS_H

#include <string>
#include <tuple>

namespace redshow {

#define MIN2(x, y) (x > y ? y : x)
#define MAX2(x, y) (x > y ? x : y)

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

// We will change the bits after this index to 0. F32:23, F64:52
const int VALID_FLOAT_DIGITS = 23;
const int VALID_DOUBLE_DIGITS = 52;

const int MIN_FLOAT_DIGITS = 20;
const int MIN_DOUBLE_DIGITS = 46;

const int LOW_FLOAT_DIGITS = 17;
const int LOW_DOUBLE_DIGITS = 40;

const int MID_FLOAT_DIGITS = 14;
const int MID_DOUBLE_DIGITS = 34;

const int HIGH_FLOAT_DIGITS = 11;
const int HIGH_DOUBLE_DIGITS = 28;

const int MAX_FLOAT_DIGITS = 8;
const int MAX_DOUBLE_DIGITS = 22;

const int SHARED_MEMORY_OFFSET = 4;
const int LOCAL_MEMORY_OFFSET = 4;
const int GLOBAL_MEMORY_OFFSET = 8;

struct ThreadId {
  u32 flat_block_id;
  u32 flat_thread_id;

  bool operator<(const ThreadId &o) const {
    return (this->flat_block_id < o.flat_block_id) ||
           (this->flat_block_id == o.flat_block_id && this->flat_thread_id < o.flat_thread_id);
  }

  bool operator==(const ThreadId &o) const {
    return this->flat_thread_id == o.flat_thread_id && this->flat_block_id == o.flat_block_id;
  }
};

/**
 * Use decimal_degree_f32 bits to cut the valid floating number bits.
 * @arg decimal_degree_f32: the valid bits. The floating numbers have 23-bit fractions.
 * */
u64 value_to_float(u64 a, int decimal_degree_f32);

/**
 * @arg decimal_degree_f64: the valid bits. The float64 numbers have 52-bit fractions.
 * */
u64 value_to_double(u64 a, int decimal_degree_f64);

}  // namespace redshow

#endif  // REDSHOW_UTILS_H
