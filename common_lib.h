//
// Created by find on 19-7-1.
//

#ifndef CUDA_REDSHOW_COMMON_LIB
#define CUDA_REDSHOW_COMMON_LIB

#include <string>
#include <regex>
#include <list>
#include <map>
#include <numeric>
#include <fstream>
#include <set>
#include <regex>

#include <tuple>
#include <algorithm>
#include <iostream>
// namespace
using std::ifstream;
using std::string;
using std::regex;
using std::ostream;
using std::tuple;
using std::get;
using std::list;
using std::map;
using std::vector;
using std::accumulate;
using std::cout;
using std::make_tuple;
using std::ofstream;
using std::to_string;
using std::set;
using std::regex;
using std::regex_match;
using std::smatch;
using std::endl;
using std::stoi;
using std::pair;
using std::distance;
using std::find;
using std::tuple;
using std::max;
using std::get;
using std::find_if;
using std::make_pair;
using std::hex;
using std::dec;

typedef unsigned char _u8;
typedef unsigned int _u32;
typedef unsigned long long _u64;
#define MEM_WRITE 2
#define MEM_READ 1
#define WARP_SIZE 32
#define CACHE_LINE_BYTES_BIN 5
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
inline long long pow10(int b) {
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
inline std::tuple<long long, long long> float2tuple(float a, int decimal_degree) {
    int b = int(a);
    int c = (int) ((a - b) * pow10(decimal_degree));
    return std::make_tuple(b, c);
}

inline bool equal_2_tuples(tuple<long long, long long, _u64> a, tuple<long long, long long> b) {
    return get<0>(a) == get<0>(b) && get<1>(a) == get<1>(b);
}


// Temporal Redundancy-Address
void get_tra_trace_map(ThreadId tid, _u64 addr, int acc_type, int belong, map<ThreadId, list<_u64 >> &tra_list,
                       map<_u64, vector<int >> &tra_trace_map, map<int, map<int, _u64 >> &tra_rd_dist);

double show_tra_redundancy(_u64 index, ThreadId &threadid_max, map<_u64, vector<int >> &tra_trace_map,
                           map<int, map<int, _u64 >> &tra_rd_dist);

//Silent load
void get_trv_r_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> &value,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_read,
                         long long &silent_load_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_load_pairs);

//dead store & silent store
void get_trv_w_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_write,
                         long long &silent_write_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_write_pairs,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_read,
                         long long &dead_write_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &dead_write_pairs);

// calculate the rates and write these pairs
void show_trv_redundancy_rate(_u64 line_num, long long &silent_load_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_load_pairs,
                              long long &silent_write_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_write_pairs,
                              long long &dead_write_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &dead_write_pairs);
// get Spatial Redundancy Address (Memory Divergence)
void get_srag_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr,
                        map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map);
void show_srag_redundancy(map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map, ThreadId &threadid_max,
                          _u64 (&srag_distribution)[WARP_SIZE]);
#endif //CUDA_REDSHOW_COMMON_LIB


