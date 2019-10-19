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
typedef unsigned char _u8;
typedef unsigned int _u32;
typedef unsigned long long _u64;


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

// convert an hex value to float
inline float store2float(_u64 a){
    _u32 c = 0;
    c= ((a&0xffu)<<24 )|((a&0xff00u)<<8) | ((a&0xff0000u)>>8) | ((a&0xff000000u)>>24);
    float b;
//    @todo Need to fix the data width.
    memcpy(&b, &c, sizeof(b));
    return b;
}

inline int store2int(_u64 a){
    int c = 0;
    c= ((a&0xffu)<<24 )
            |((a&0xff00)<<8) | ((a&0xff0000)>>8) | ((a&0xff000000)>>24);
    return c;
}
#endif //CUDA_REDSHOW_COMMON_LIB
