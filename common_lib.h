//
// Created by find on 19-7-1.
//

#ifndef CUDA_REDSHOW_COMMON_LIB
#define CUDA_REDSHOW_COMMON_LIB

#include <string>
#include <regex>



// namespace
using std::ifstream;
using std::string;
using std::regex;
using std::ostream;
using std::tuple;
using std::get;
typedef unsigned char _u8;
typedef unsigned int _u32;
typedef unsigned long long _u64;
#define MEM_WRITE 2
#define MEM_READ 1
// The values' types.
enum BasicType {
    F32, F64, S64, U64, S32, U32, S8, U8
};


class ThreadId {
public:
    int bx;
    int by;
    int bz;
    int tx;
    int ty;
    int tz;

    bool operator<(const ThreadId &o) const {
        return bz < o.bz || by < o.by || bx < o.bx || tz < o.tz || ty < o.ty || tx < o.tx;
    }

    bool operator==(const ThreadId &o) const {
        return bz == o.bz && by == o.by && bx == o.bx && tz == o.tz && ty == o.ty && tx == o.tx;
    }
};


class Variable {
public:
    long long start_addr;
    int size;
    int flag;
    std::string var_name;
    int size_per_unit;
};
// double type has a precision of 53bits (about 16 decimal digits). 10^16 is bigger than INT_MAX.
inline long long pow10(int b){
    long long r = 1;
    for (int i = 0; i < b; ++i) {
        r *= 10;
    }
    return r;
}
// convert an hex value to float
inline float store2float(_u64 a) {
    _u32 c = 0;
    c = ((a & 0xffu) << 24) | ((a & 0xff00u) << 8) | ((a & 0xff0000u) >> 8) | ((a & 0xff000000u) >> 24);
    float b;
//    @todo Need to fix the data width.
    memcpy(&b, &c, sizeof(b));
    return b;
}

inline int store2int(_u64 a) {
    int c = 0;
    c = ((a & 0xffu) << 24)
        | ((a & 0xff00) << 8) | ((a & 0xff0000) >> 8) | ((a & 0xff000000) >> 24);
    return c;
}
/**@arg decimal_degree: the precision of the result should be*/
inline std::tuple<long long, long long> float2tuple(float a, int decimal_degree){
    int b = int(a);
    int c = (int)((a-b)*pow10(decimal_degree));
    return std::make_tuple(b, c);
}

inline bool equal_2_tuples(tuple<long long, long long, _u64 >a, tuple<long long, long long>b){
    return get<0>(a) == get<0>(b) && get<1>(a) == get<1>(b);
}

#endif //CUDA_REDSHOW_COMMON_LIB
