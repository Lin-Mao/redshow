//
// Created by find on 19-7-1.
//

#ifndef REDSHOW_COMMON_LIB_H
#define REDSHOW_COMMON_LIB_H

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
using std::memcpy;

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

    bool operator<(const ThreadId &o) const;

    bool operator==(const ThreadId &o) const;
};


inline _u64 store2uchar(_u64 a) {
    return a & 0xffu;
}


inline _u64 store2uint(_u64 a) {
    _u64 c = 0;
    c = ((a & 0xffu) << 24u)
        | ((a & 0xff00u) << 8u) | ((a & 0xff0000u) >> 8u) | ((a & 0xff000000u) >> 24u);
    return c;
}

// convert an hex value to float format. Use decimal_degree_f32 to control precision. We still store
inline _u64 store2float(_u64 a, int decimal_degree_f32) {
    _u32 c = store2uint(a);
    _u32 mask = 0xffffffff;
    for (int i = 0; i < 23 - decimal_degree_f32; ++i) {
        mask <<= 1u;
    }
    c &= mask;
    _u64 b = 0;
    memcpy(&b, &c, sizeof(c));
    return b;
}

// Mainly change the expression from big endian to little endian.
inline _u64 store2u64(_u64 a) {
    _u64 c = 0;
    c = ((a & 0xffu) << 56u) | ((a & 0xff00u) << 40u) | ((a & 0xff0000u) << 24u) | ((a & 0xff000000u) << 8u) |
        ((a & 0xff00000000u) >> 8u) | ((a & 0xff0000000000u) >> 24u) | ((a & 0xff000000000000u) >> 40u) |
        ((a & 0xff00000000000000u) >> 56u);
    return c;
}

inline _u64 store2double(_u64 a, int decimal_degree_f64) {
    _u64 c = store2u64(a);
    _u64 mask = 0xffffffffffffffff;
    for (int i = 0; i < 52 - decimal_degree_f64; ++i) {
        mask <<= 1u;
    }
    c = c & mask;
    return c;
}


inline bool equal_2_tuples(tuple<long long, long long, _u64> a, tuple<long long, long long> b) {
    return get<0>(a) == get<0>(b) && get<1>(a) == get<1>(b);
}


// Temporal Redundancy-Address
void get_tra_trace_map(ThreadId tid, _u64 addr, int acc_type, int belong, map<ThreadId, list<_u64 >> &tra_list,
                       map<_u64, vector<int >> &tra_trace_map, map<int, map<int, _u64 >> &tra_rd_dist);

void show_tra_redundancy(_u64 index, ThreadId &threadid_max, map<_u64, vector<int >> &tra_trace_map,
                         map<int, map<int, _u64 >> &tra_rd_dist);

//Silent load
void get_trv_r_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value,
                         map<ThreadId, map<_u64, tuple<_u64, _u64, _u64>>> &trv_map_read,
                         long long &silent_load_num,
                         vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &silent_load_pairs, BasicType atype);

//dead store & silent store
void get_trv_w_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value,
                         map<ThreadId, map<_u64, tuple<_u64, _u64, _u64>>> &trv_map_write,
                         long long &silent_write_num,
                         vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &silent_write_pairs,
                         map<ThreadId, map<_u64, tuple<_u64, _u64, _u64>>> &trv_map_read,
                         long long &dead_write_num,
                         vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &dead_write_pairs, BasicType atype);

// calculate the rates and write these pairs
void show_trv_redundancy_rate(_u64 line_num, long long &silent_load_num,
                              vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &silent_load_pairs,
                              long long &silent_write_num,
                              vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &silent_write_pairs,
                              long long &dead_write_num,
                              vector<tuple<_u64, _u64, _u64, _u64, BasicType>> &dead_write_pairs);

// get Spatial Redundancy Address (Memory Divergence)
void get_srag_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr,
                        map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map);

void show_srag_redundancy(map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map, ThreadId &threadid_max,
                          _u64 (&srag_distribution)[WARP_SIZE]);

void get_srv_ws_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value, int belong,
                          map<ThreadId, map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>>> &srv_bs_trace_map);

tuple<int, _u64> get_cur_addr_belong_index(_u64 addr, vector<tuple<_u64, int>> &vars_mem_block);

int get_cur_addr_belong(_u64 addr, vector<tuple<_u64, int>> &vars_mem_block);

void get_dc_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value, vector<tuple<_u64, int>> &vars_mem_block,
                      map<int, set<_u64 >> &dc_trace_map);

void get_hr_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value, int belong, set<_u64> &pcs,
                      map<int, map<_u64, _u64>> &hr_trace_map,
                      map<_u64, map<int, map<_u64, _u64 >>> &hr_trace_map_pc_dist);

void show_hr_redundancy(map<int, map<_u64, _u64>> &hr_trace_map, set<_u64> &pcs, const BasicType *vars_type);

inline void output_corresponding_type_value(_u64 a, BasicType atype, ofstream &out) {
    switch (atype) {
        case F32:
            float b1;
            memcpy(&b1, &a, sizeof(b1));
            out << b1;
            break;
        case F64:
            double b2;
            memcpy(&b2, &a, sizeof(b2));
            out << b2;
            break;
        case S64:
            long long b3;
            memcpy(&b3, &a, sizeof(b3));
            break;
        case U64:
            out << a;
            break;
        case S32:
            int b4;
            memcpy(&b4, &a, sizeof(b4));
            out << b4;
            break;
        case U32:
            _u32 b5;
            memcpy(&b5, &a, sizeof(b5));
            out << b5;
            break;
        case S8:
            char b6;
            memcpy(&b6, &a, sizeof(b6));
            out << b6;
            break;
        case U8:
            _u8 b7;
            memcpy(&b7, &a, sizeof(b7));
            out << b7;
            break;
    }
}

inline void output_corresponding_type_value_cout(_u64 a, BasicType atype) {
    switch (atype) {
        case F32:
            float b1;
            memcpy(&b1, &a, sizeof(b1));
            cout << b1;
            break;
        case F64:
            double b2;
            memcpy(&b2, &a, sizeof(b2));
            cout << b2;
            break;
        case S64:
            long long b3;
            memcpy(&b3, &a, sizeof(b3));
            break;
        case U64:
            cout << a;
            break;
        case S32:
            int b4;
            memcpy(&b4, &a, sizeof(b4));
            cout << b4;
            break;
        case U32:
            _u32 b5;
            memcpy(&b5, &a, sizeof(b5));
            cout << b5;
            break;
        case S8:
            char b6;
            memcpy(&b6, &a, sizeof(b6));
            cout << b6;
            break;
        case U8:
            _u8 b7;
            memcpy(&b7, &a, sizeof(b7));
            cout << b7;
            break;
    }
}

#endif  // REDSHOW_COMMON_LIB_H


