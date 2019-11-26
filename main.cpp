#include <iostream>
#include "common_lib.h"
#include <map>
#include <fstream>
#include <set>
#include <regex>
#include <list>
#include <numeric>
#include <tuple>
#include <algorithm>
#include "cxxopts.hpp"

using std::set;
using std::regex;
using std::map;
using std::regex_match;
using std::smatch;
using std::cout;
using std::endl;
using std::stoi;
using std::vector;
using std::pair;
using std::distance;
using std::list;
using std::find;
using std::accumulate;
using std::tuple;
using std::max;
using std::get;
using std::find_if;
using std::make_pair;
using std::hex;
using std::dec;
using cxxopts::Options;
using std::make_tuple;
using std::ofstream;
using std::to_string;

void init();

ThreadId transform_tid(string bid, string tid);

void read_input_file(string input_file, string target_name);

ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max);

// Temporal Redundancy-Address
void get_tra_trace_map(ThreadId tid, _u64 addr, int acc_type, int belong);

double calc_tra_redundancy_rate(_u64 index);

// Temporal Redundancy-Value
void get_trv_r_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value);

// detect silent write and dead write
void get_trv_w_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value);

double calc_trv_redundancy_rate(_u64 line_num);

// Spatial Redundancy Address Global Memory
void get_srag_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value);

void get_srag_trace_map_test(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value);

void calc_srag_redundancy_degree(_u64 index);

double calc_srag_redundancy_degree_test(_u64 index);

// Spatial Redundancy Address Shared memory
void get_sras_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value);

pair<_u64, double> calc_sras_redundancy_rate(_u64 index);

// Spatial Redundancy Value
void get_srv_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value);

void calc_srv_redundancy_rate(_u64 index);

// Vertical Redundancy
void get_vr_trace_map(_u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value,
                      BasicType acc_type);

void calc_vr_redundancy_rate(_u64 index);

int get_cur_addr_belong(_u64 addr);

// dead copy means copy the array but some part of this array doesn't use
void get_dc_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value);

void filter_dead_copy();

void analysis_hr_red_result();

// This block is the data definitions
// Every thread has a ordered set to save the read addresses.
map<ThreadId, list<_u64 >> tra_list;
// trace_map: {addr1 : [rd1, rd2,], }
map<_u64, vector<int >> tra_trace_map;
// {var:{rd1: 100, rd2:100}}.
// I'm not sure whether it is good to use the second int as key . Will the rd are over than int_max?
map<int, map<int, _u64 >> tra_rd_dist;
// Temporal Redundancy-Value dict_queue:{thread: {addr1: <val1, index>,}}
// The last index variable is used to mark the offset in trace file which is used to detect dead store.
// If val1 is float, it's better to use two numbers to represent the value. One is the integer part and the other one in decimal part
// This map records a thread's last access.
map<ThreadId, map<_u64, tuple<long long, long long, _u64>>> trv_map_read;
map<ThreadId, map<_u64, tuple<long long, long long, _u64>>> trv_map_write;
// save every dead read and write 's index of input file.
vector<_u64> silent_load_index;
vector<_u64> silent_write_index;
vector<_u64> dead_write_index;
long long silent_load_num, dead_write_num, silent_write_num;
//Spatial Redundancy Address Global memory
//@todo At this time, we don't know every variable's addr range, so we regard all addr as global or shared
// {pc: {tid: [(index, addr, value), ]}}
map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>> srag_trace_map;
//// {pc: {tid: {addr,}, ]}}
map<_u64, map<ThreadId, set<_u64>>> srag_trace_map_test;
//{pc:{value:{thread:num}}}
map<_u64, map<_u64, map<ThreadId, _u64 >>> srv_trace_map;
// Final histogram of global memory access distribution. index i means there is i+1 unique cache lines.
_u64 srag_distribution[32];

// the sizes of thread block and grid
ThreadId threadid_max;
Options options("CUDA_RedShow", "A test suit for hpctoolkit santizer");
// a new category
// I think pc is not used at this time.
// vertical redundancy:{thread: {pc: {value:times}}}
//map<ThreadId, map<_u64, map<_u64, _u64 >>> vr_trace_map;
// vertical redundancy:{thread: {value:times}}
map<ThreadId, map<tuple<long long, long long>, _u64 >> vr_trace_map;
// horizontal redundancy
// every array's memory start addr and size
vector<tuple<_u64, int>> vars_mem_block;
// the values' type of every array
//vector<BasicType> vars_type;
// 先这样写死，后面再改成上面的vector形式
BasicType vars_type[12] = {F32, S32, S32, S32, S32, F32, F32, F32, F32, F32, F32, F32};
//{var:{value:num}}
map<int, map<tuple<long long, long long>, _u64>> hr_trace_map;
// {pc: { var:{value: num} }}
map<_u64 , map<int, map<tuple<long long, long long>, _u64 >>> hr_trace_map_pc_dist;
// to get the number of pcs
set<_u64> pcs;
//at this time, I use vector as the main format to store tracemap, but if we can get an input of array size, it can be changed to normal array.
// {var:{index1,index2}}
map<int, set<_u64 >> dc_trace_map;
// We will ignore the digits after this index in decimal part.
int valid_float_digits = 5;


regex line_read_re("0x(.+?)\\|\\((.+?)\\)\\|\\((.+?)\\)\\|0x(.+?)\\|0x(.+)\\|(.+)");
regex tid_re("(\\d+),(\\d+),(\\d+)");
//Allocate memory address 0x7fe39b600000, size 40
//used to filter memory allocation information
regex log_read_re("Allocate memory address 0x(.+), size (\\d+)");

ostream &operator<<(ostream &out, const ThreadId &A) {
    out << "(" << A.bx << "," << A.by << "," << A.bz << ")(" << A.tx << "," << A.ty << "," << A.tz << ")";
    return out;
}

void init() {
    silent_load_num = 0;
    dead_write_num = 0;
    options.add_options()
            ("i,input", "Input trace file", cxxopts::value<std::string>());
    options.add_options()
            ("l,log", "Input log file", cxxopts::value<std::string>());

    memset(srag_distribution, 0, sizeof(_u64) * 32);
}

ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max) {
    threadid_max.bz = max(threadid_max.bz, a.bz);
    threadid_max.by = max(threadid_max.by, a.by);
    threadid_max.bx = max(threadid_max.bx, a.bx);
    threadid_max.tz = max(threadid_max.tz, a.tz);
    threadid_max.ty = max(threadid_max.ty, a.ty);
    threadid_max.tx = max(threadid_max.tx, a.tx);
    return threadid_max;
}

ThreadId transform_tid(string s_bid, string s_tid) {
// This function will transform the raw string of bid and tid to struct ThreadId
// @arg s_bid: (2,0,0): (bx,by,bz)
    ThreadId tid = {-1, -1, -1, -1, -1, -1};
    smatch sm;
    regex_match(s_bid, sm, tid_re);
    if (sm.empty()) {
        return tid;
    }
    tid.bx = stoi(sm[1], 0, 10);
    tid.by = stoi(sm[2], 0, 10);
    tid.bz = stoi(sm[3], 0, 10);
    regex_match(s_tid, sm, tid_re);
    if (sm.empty()) {
        return tid;
    }
    tid.tx = stoi(sm[1], 0, 10);
    tid.ty = stoi(sm[2], 0, 10);
    tid.tz = stoi(sm[3], 0, 10);
    return tid;
}

/**This function can calculate the reuse distance of every addr.
 * @arg acc_type : If the operation of this addr is write, the current reuse distance counter of this addr will be clear. 1 is read and 2 is write
 * @arg belong : It is the index of the array current addr belonging to.
 * I'm not sure whether we should change int to int8.*/
void get_tra_trace_map(ThreadId tid, _u64 addr, int acc_type, int belong) {
    map<ThreadId, list<_u64 >>::iterator tl_it;
    tl_it = tra_list.find(tid);
//  A new thread occurs. We didn't see this thread before.
    if (tl_it == tra_list.end()) {
        list<_u64> tmp;
        tmp.push_back(addr);
        tra_list.insert(pair<ThreadId, list<_u64>>(tid, tmp));
    } else {
//        This thread has a list to save the reuse distances
        list<_u64>::iterator l_it;
        l_it = find(tl_it->second.begin(), tl_it->second.end(), addr);
        int tmp_rd = -1;

//        if the addr is in thread's set, calc the rd and insert the rd into trace_map
        if (l_it != tl_it->second.end()) {
            if (acc_type == MEM_READ) {
                tmp_rd = distance(l_it, tl_it->second.end()) - 1;
                auto ttm_it = tra_trace_map.find(addr);
                if (ttm_it == tra_trace_map.end()) {
                    vector<int> tmp_vector;
                    tmp_vector.push_back(tmp_rd);
                    tra_trace_map.insert(pair<_u64, vector<int>>(addr, tmp_vector));
                } else {
                    ttm_it->second.push_back(tmp_rd);
                }
//                 update the log of rd distributions
                auto rd_it = tra_rd_dist.find(tmp_rd);
                if (rd_it == tra_rd_dist.end()) {
                    map<int, _u64> map1 = {{tmp_rd, 1},};
                    tra_rd_dist[belong] = map1;
                } else {
                    _u64 rd_times = rd_it->second[tmp_rd];
                    rd_it->second[tmp_rd] = rd_times + 1;
                }
            }
//            no matter read or write, we need to remove the addr from thread's list and append new addr(same addr) in the tail of the list.
            if (tmp_rd != 0) {
                tl_it->second.remove(addr);
            }
//            Should I check the value not 1,2?
        }
        if (tmp_rd != 0) {
            tl_it->second.push_back(addr);
        }
    }
}

/**@arg index: It is used to mark the silent load offset in the trace file. It is also used to mark the last read which is used to */
void get_trv_r_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value) {
    auto tmr_it = trv_map_read.find(tid);
    map<_u64, tuple<long long, long long, _u64 >> record;
    record.insert(
            pair<_u64, tuple<long long, long long, _u64 >>(addr, make_tuple(get<0>(value), get<1>(value), index)));
//    The trv_map_read doesn't have the thread's record
    if (tmr_it == trv_map_read.end()) {
        trv_map_read.insert(pair<ThreadId, map<_u64, tuple<long long, long long, _u64 >>>(tid, record));
    } else {
        auto m_it = tmr_it->second.find(addr);
//      The trv_map_read's thread record doesn't have the current addr record.
        if (m_it == tmr_it->second.end()) {
            tmr_it->second.insert(pair<_u64, tuple<long long, long long, _u64 >>(addr, record[addr]));
        } else {
            if (equal_2_tuples(m_it->second, value)) {
                silent_load_num++;
                silent_load_index.push_back(index);
            }
            m_it->second = record[addr];
        }
    }
}

/**@arg index: It is used to mark the silent write and dead write offset in the trace file. */
void get_trv_w_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value) {
    auto tmw_it = trv_map_write.find(tid);
    map<_u64, tuple<long long, long long, _u64 >> record;
    record.insert(
            pair<_u64, tuple<long long, long long, _u64 >>(addr, make_tuple(get<0>(value), get<1>(value), index)));
    if (tmw_it == trv_map_write.end()) {
        trv_map_write.insert(pair<ThreadId, map<_u64, tuple<long long, long long, _u64 >>>(tid, record));
    } else {
        auto m_it = tmw_it->second.find(addr);
        if (m_it == tmw_it->second.end()) {
            tmw_it->second.insert(pair<_u64, tuple<long long, long long, _u64 >>(addr, record[addr]));
        } else {
            if (equal_2_tuples(m_it->second, value)) {
                silent_write_num++;
                silent_write_index.push_back(index);
            }
//            check the last read about this <thread, addr>
            auto tmr_it = trv_map_read.find(tid);
            if (tmr_it != trv_map_read.end()) {
                auto m_it2 = tmr_it->second.find(addr);
                if (m_it2 != tmr_it->second.end()) {
//                    tuple<long long, long long, _u64 > the last one is index of this read.
                    auto r_value = m_it2->second;
//                  the last read of this addr is earlier than last write.
                    if (get<2>(r_value) < get<2>(m_it->second)) {
                        dead_write_num++;
                        dead_write_index.push_back(index);
                    }
                }
            }
            m_it->second = record[addr];
        }
    }
}

/**@arg: index, if there are loops in original code, every pc will own lot of access in same thread. The index is similar to timestamp to clarify which iteration the current access in.*/
void get_srag_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value) {
//    stm_it->second is a tuple: <_u64, _u64, _u64>(index, addr, value)
    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
    stm_it = srag_trace_map.find(pc);
    if (stm_it == srag_trace_map.end()) {
        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>> tmp = {{tid, vector<tuple<_u64, _u64, _u64 >>{
                tuple<_u64, _u64, _u64>(index, addr, value)},}};
        srag_trace_map.insert(pair<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64>>>>(pc, tmp));
    } else {
        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>::iterator m_it;
        m_it = stm_it->second.find(tid);
//      stm has addr key but inner doesn't have tid key
        if (m_it == stm_it->second.end()) {
            vector<tuple<_u64, _u64, _u64 >> tmp_1 = {tuple<_u64, _u64, _u64>(index, addr, value)};
            stm_it->second.insert(pair<ThreadId, vector<tuple<_u64, _u64, _u64 >>>(tid, tmp_1));
        } else {
            m_it->second.emplace_back(tuple<_u64, _u64, _u64>(index, addr, value));
        }

    }

}

void get_srag_trace_map_test(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value) {
//    tuple: <_u64, _u64, _u64>(index, addr, value)
    map<_u64, map<ThreadId, set<_u64 >>>::iterator stm_it;
    stm_it = srag_trace_map_test.find(pc);
    if (stm_it == srag_trace_map_test.end()) {
        map<ThreadId, set<_u64 >> tmp;
        set<_u64> tmp_s;
        tmp_s.insert(addr);
        tmp.insert(pair<ThreadId, set<_u64 >>(tid, tmp_s));
        srag_trace_map_test.insert(pair<_u64, map<ThreadId, set<_u64>>>(pc, tmp));
    } else {
        map<ThreadId, set<_u64 >>::iterator m_it;
        m_it = stm_it->second.find(tid);
//      stm has addr key but inner doesn't have tid key
        if (m_it == stm_it->second.end()) {
            set<_u64> tmp_1;
            tmp_1.insert(addr);
            stm_it->second.insert(pair<ThreadId, set<_u64>>(tid, tmp_1));
        } else {
            m_it->second.insert(addr);
        }

    }

}

void get_sras_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value) {
//    tuple: <_u64, _u64, _u64>(index, addr, value)
    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
    stm_it = srag_trace_map.find(pc);
    if (stm_it == srag_trace_map.end()) {
        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>> tmp = {{tid, vector<tuple<_u64, _u64, _u64 >>{
                tuple<_u64, _u64, _u64>(index, addr, value)},}};
        srag_trace_map.insert(pair<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64>>>>(pc, tmp));
    } else {
        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>::iterator m_it;
        m_it = stm_it->second.find(tid);
//      stm has addr key but inner doesn't have tid key
        if (m_it == stm_it->second.end()) {
            vector<tuple<_u64, _u64, _u64 >> tmp_1 = {tuple<_u64, _u64, _u64>(index, addr, value)};
            stm_it->second.insert(pair<ThreadId, vector<tuple<_u64, _u64, _u64 >>>(tid, tmp_1));
        } else {
            m_it->second.emplace_back(tuple<_u64, _u64, _u64>(index, addr, value));
        }

    }

}
// commented by FindHao. This function is for the vertical red. We first talk about it in 9.20.
//void get_vr_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value) {
//    auto vrtm_it = vr_trace_map.find(tid);
//    if (vrtm_it == vr_trace_map.end()) {
//        map<_u64, map<_u64, _u64 >> tmp = {{pc, map<_u64, _u64>{pair<_u64, _u64>(value, 1)}}};
//        vr_trace_map.insert(pair<ThreadId, map<_u64, map<_u64, _u64>>>(tid, tmp));
//    } else {
////        m_it is {pc:{value:number}}
//        auto m_it = vrtm_it->second.find(pc);
//        if (m_it == vrtm_it->second.end()) {
//            map<_u64, _u64> tmp_1 = {pair<_u64, _u64>(value, 1)};
//            vrtm_it->second.insert(pair<_u64, map<_u64, _u64>>(pc, tmp_1));
//        } else {
//            auto v_it = m_it->second.find(value);
//            if (v_it == m_it->second.end()) {
//                m_it->second.insert(pair<_u64, _u64>(value, 1));
//            } else {
//                m_it->second[value] += 1;
//            }
//        }
//    }
//}

void get_vr_trace_map(_u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value,
                      BasicType acc_type) {
    auto vrtm_it = vr_trace_map.find(tid);
    if (vrtm_it == vr_trace_map.end()) {
        map<tuple<long long, long long>, _u64 > tmp_1 = {pair<tuple<long long, long long>, _u64>(value, 1)};
        vr_trace_map.insert(pair<ThreadId, map<tuple<long long, long long>, _u64>>(tid, tmp_1));
    } else {
        auto v_it = vrtm_it->second.find(value);
        if (v_it == vrtm_it->second.end()) {
            vrtm_it->second.insert(pair<tuple<long long, long long>, _u64>(value, 1));
        } else {
            vrtm_it->second[value] += 1;
        }
    }
}

void get_srv_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value) {
    auto stm_it = srv_trace_map.find(pc);
    if (stm_it == srv_trace_map.end()) {
        map<_u64, map<ThreadId, _u64 >> tmp = {{value, map<ThreadId, _u64>{
                pair<ThreadId, _u64>(tid, 1)},}};
        srv_trace_map.insert(pair<_u64, map<_u64, map<ThreadId, _u64>>>(pc, tmp));
    } else {
//        m_it is map: {value: {thread:num}}
        auto m_it = stm_it->second.find(value);
        if (m_it == stm_it->second.end()) {

            map<ThreadId, _u64> tmp_1 = {pair<ThreadId, _u64>(tid, 1)};
            stm_it->second.insert(pair<_u64, map<ThreadId, _u64 >>(value, tmp_1));
        } else {
//            has the value mapping
//            update the statistics
//            auto v_it = find_if( m_it->second.begin(), m_it->second.end(),[&tid](const pair<ThreadId, _u64 >& element){ return element.first == tid;} );
            auto it = m_it->second.find(tid);
            if (it == m_it->second.end()) {
                m_it->second.insert(pair<ThreadId, _u64>(tid, 1));
            } else {
                m_it->second[tid] += 1;
            }
        }

    }

}

void get_hr_trace_map(_u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value, int belong) {
    pcs.insert(pc);
//   //{var:{value:num}} map<int, map<_u64, _u64>> hr_trace_map;
    hr_trace_map[belong][value] += 1;
//  {pc: { var:{value: num} }}
    auto hrpc_it1 = hr_trace_map_pc_dist.find(pc);
    hr_trace_map_pc_dist[pc][belong][value] += 1;
}

/**e.g. a[100] belongs to array a
 * @return -1 not found
 * @return -2 find over 1 arrars it belongs
 * */
int get_cur_addr_belong(_u64 addr) {
    using std::get;
    int belong = -1;
    for (auto it = vars_mem_block.begin(); it != vars_mem_block.end(); ++it) {
        if (addr >= get<0>(*it) && addr < (get<0>(*it) + get<1>(*it))) {
            if (belong != -1) {
//                cout<<"Error:\tAn addr belongs to over 1 array"<<endl;
                return -2;
            } else {
                belong = it - vars_mem_block.begin();
            }
        }
    }
    return belong;
}

/**e.g. a[100] belongs to array a
 * @return (-1,0) not found
 * @return (-2,0) find over 1 arrars it belongs
 * @return (X,Y) the addr belongs to array X, and its index is Y
 * We assume the unit of array is 4bytes at this moment
 * */
tuple<int, _u64> get_cur_addr_belong_index(_u64 addr) {
    using std::get;
    int belong = -1;
//    initialize it to -1 to make it a larger number
    _u64 index = -1;
    for (auto it = vars_mem_block.begin(); it != vars_mem_block.end(); ++it) {
        if (addr >= get<0>(*it) && addr < (get<0>(*it) + get<1>(*it))) {
            if (belong != -1) {
//                cout<<"Error:\tAn addr belongs to over 1 array"<<endl;
                return make_tuple(-2, 0);
            } else {
                belong = it - vars_mem_block.begin();
//                @todo We assume the unit of array is 4bytes at this moment
                index = (addr - get<0>(*it)) / 4;
            }
        }
    }
    return make_tuple(belong, index);
}

void get_dc_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value) {
    auto belongs = get_cur_addr_belong_index(addr);
    int belong = get<0>(belongs);
    _u64 index = get<1>(belongs);
    switch (belong) {
        case -1 :
            cout << "addr " << hex << addr << dec << " not found which array it belongs to" << endl;
            return;
        case -2:
            cout << "addr " << hex << addr << dec << " found over 1 array it belongs to" << endl;
            return;
    }
    // {var:{index1,index2}}
    auto dc_it = dc_trace_map.find(belong);
    if (dc_it == dc_trace_map.end()) {
        set<_u64> tmp_1;
        tmp_1.insert(index);
        dc_trace_map.insert(pair<int, set<_u64> >(belong, tmp_1));
    } else {
        dc_it->second.insert(index);
    }
}

void filter_dead_copy() {
    // {var:{index1,index2}}
    for (auto dc_it = dc_trace_map.begin(); dc_it != dc_trace_map.end(); ++dc_it) {
        cout << "The array " << dc_it->first << " use rate: " << dc_it->second.size() * 1.0 << " / "
             << (*dc_it->second.rbegin() + 1)
             << " = " << dc_it->second.size() * 1.0 / (*dc_it->second.rbegin() + 1) << endl;
    }
}

double calc_tra_redundancy_rate(_u64 index) {
    double tra_rate = 0;
    long long r_sum = 0;
//    How many reuse distance has recorded.
    long long r_num = 0;
    int thread_nums =
            (threadid_max.tx + 1) * (threadid_max.ty + 1) * (threadid_max.tz + 1) * (threadid_max.bx + 1) *
            (threadid_max.by + 1) * (threadid_max.bz + 1);
    map<_u64, vector<int >>::iterator ttm_it;
    for (ttm_it = tra_trace_map.begin(); ttm_it != tra_trace_map.end(); ttm_it++) {
        r_sum += accumulate(ttm_it->second.begin(), ttm_it->second.end(), 0);
        r_num += ttm_it->second.size();
    }
    tra_rate = thread_nums == 0 ? 0 : (double) r_sum / thread_nums;
    cout << "reuse distance sum:\t" << r_sum << endl;
    cout << "reuse time rate:\t" << (double) r_num / index << endl;
//    cout << tra_rate << endl;
// write the reuse distance histogram to csv files.
//    ofstream out("tra_all.csv");
    for (int i = 0; i < 12; ++i) {
        ofstream out("tra_" + to_string(i) + ".csv");
//        for (auto tra_it: tra_rd_dist) {
//
//        }
        auto tmp_rd_dist = tra_rd_dist[i];
        for (auto every_rd:tmp_rd_dist) {
            out << every_rd.first << "," << every_rd.second << endl;
        }
        out.close();
    }
    return tra_rate;

}

double calc_trv_redundancy_rate(_u64 line_num) {
    if (line_num == 0) return 0;
    cout << "silent_load_num\t" << silent_load_num << endl;
    cout << "silent_load rate\t" << (double) silent_load_num / line_num;
    cout << "silent_write_num\t" << silent_write_num << endl;
    cout << "silent_write rate\t" << (double) silent_write_num / line_num;
    cout << "dead_write_num\t" << dead_write_num << endl;
    cout << "dead_write rate\t" << (double) dead_write_num / line_num;

}

void calc_srag_redundancy_degree(_u64 index) {
//    _u64 all_transactions = 0;
    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
    int bz, by, bx, tz, ty, tx;
    for (stm_it = srag_trace_map.begin(); stm_it != srag_trace_map.end(); stm_it++) {
        for (bz = 0; bz <= threadid_max.bz; bz++) {
            for (by = 0; by <= threadid_max.by; ++by) {
                for (bx = 0; bx <= threadid_max.bx; ++bx) {
                    for (tz = 0; tz <= threadid_max.tz; ++tz) {
                        for (ty = 0; ty <= threadid_max.ty; ++ty) {
                            for (tx = 0; tx <= threadid_max.tx; tx += 32) {
                                int remain_items = 0;
                                for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                    ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                    m_it: map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>
                                    auto m_it = stm_it->second.find(tmp_id);
                                    if (m_it != stm_it->second.end()) {
                                        remain_items = max(remain_items, (int) m_it->second.size());
                                    }
                                }
//                                if (remain_items > 1) { cout << "remain_items is " << remain_items << endl; }
                                for (int i = 0; i < remain_items; ++i) {
//                                    per warp, per pc, per iteration of a loop
                                    set<_u64> warp_unique_cache_lines;
                                    for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                        ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
                                        auto m_it = stm_it->second.find(tmp_id);
                                        if (m_it != stm_it->second.end() && m_it->second.size() > i) {
//                                            the tuple has three items now. We need the second one, addr
//                                      Every cache line is 32bytes now.
                                            int x = get<1>(m_it->second[i]) >> 5;
                                            warp_unique_cache_lines.insert(get<1>(m_it->second[tx_i]) >> 5);
                                        }
                                    }
//                                    all_transactions += warp_unique_cache_lines.size();
                                    int cur_warp_unique_cache_line_size = warp_unique_cache_lines.size();
                                    if (cur_warp_unique_cache_line_size > 32 || cur_warp_unique_cache_line_size <= 0) {
                                        cout << "Error: There is a warp access illegal unique cache line size "
                                             << cur_warp_unique_cache_line_size << endl;
                                    } else {
                                        srag_distribution[cur_warp_unique_cache_line_size - 1]++;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
//    double perfect_transaction = index  / 8;
//    return all_transactions / perfect_transaction;
}


double calc_srag_redundancy_degree_test(_u64 index) {
    _u64 all_transactions = 0;
    map<_u64, map<ThreadId, set<_u64>>>::iterator stm_it;
    int bz, by, bx, tz, ty, tx;
    for (stm_it = srag_trace_map_test.begin(); stm_it != srag_trace_map_test.end(); stm_it++) {
        for (bz = 0; bz <= threadid_max.bz; bz++) {
            for (by = 0; by <= threadid_max.by; ++by) {
                for (bx = 0; bx <= threadid_max.bx; ++bx) {
                    for (tz = 0; tz <= threadid_max.tz; ++tz) {
                        for (ty = 0; ty <= threadid_max.ty; ++ty) {
                            for (tx = 0; tx <= threadid_max.tx; tx += 32) {
                                int remain_items = 0;
                                for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                    ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                    m_it: map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>
                                    auto m_it = stm_it->second.find(tmp_id);
                                    if (m_it != stm_it->second.end()) {
                                        remain_items = max(remain_items, (int) m_it->second.size());
                                    }
                                }
                                if (remain_items > 1) { cout << "remain_items is " << remain_items << endl; }
//                                for (int i = 0; i < remain_items; ++i) {
////                                    per warp, per pc, per iteration of a loop
//                                    set<_u64> warp_unique_cache_lines;
//                                    for (int tx_i = 0; tx_i < 32; ++tx_i) {
//                                        ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                        auto m_it = stm_it->second.find(tmp_id);
//                                        if (m_it != stm_it->second.end() && m_it->second.size() > tx_i) {
////                                            the tuple has three items now. We need the second one, addr
////                                      Every cache line is 32bytes now.
//                                            int x = get<1>(m_it->second[tx_i]) >> 5;
//                                            warp_unique_cache_lines.insert(get<1>(m_it->second[tx_i]) >> 5);
//                                        }
//                                    }
//                                    all_transactions += warp_unique_cache_lines.size();
//                                }
                            }
                        }
                    }
                }
            }
        }
    }
    double perfect_transaction = index * 4.0 / 32;
    return all_transactions / perfect_transaction;
}

pair<_u64, double> calc_sras_redundancy_rate(_u64 index) {
    _u64 conflict_time = 0;
    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
    int bz, by, bx, tz, ty, tx;
    for (stm_it = srag_trace_map.begin(); stm_it != srag_trace_map.end(); stm_it++) {
        for (bz = 0; bz <= threadid_max.bz; bz++) {
            for (by = 0; by <= threadid_max.by; ++by) {
                for (bx = 0; bx <= threadid_max.bx; ++bx) {
                    for (tz = 0; tz <= threadid_max.tz; ++tz) {
                        for (ty = 0; ty <= threadid_max.ty; ++ty) {
                            for (tx = 0; tx <= threadid_max.tx; tx += 32) {

//                              The max number of remain items in threads' vectors. At this moment, every pc only has one access per thread. So actually, remain_items seems equal to 1.
                                int remain_items = 0;
                                for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                    ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                    m_it: map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>
                                    auto m_it = stm_it->second.find(tmp_id);
                                    if (m_it != stm_it->second.end()) {
                                        remain_items = max(remain_items, (int) m_it->second.size());
                                    }
                                }
//                                if (remain_items > 1) { cout << "remain_items is " << remain_items << endl; }
                                for (int i = 0; i < remain_items; ++i) {
                                    int this_iterations_valid_item = 32;
//                                    per warp, per pc, per iteration of a loop
                                    set<_u64> bank_visit;
                                    for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                        ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
                                        auto m_it = stm_it->second.find(tmp_id);
                                        if (m_it != stm_it->second.end() && m_it->second.size() > i) {
//                                            the tuple has three items now. We need the second one, addr
                                            bank_visit.insert(get<1>(m_it->second[i]));
                                        } else {
//                                            Not all threads in this warp works in this iteration.
                                            this_iterations_valid_item--;
                                        }
                                    }
//                                    How many conflicts
                                    conflict_time += this_iterations_valid_item - bank_visit.size();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return make_pair(conflict_time, (double) conflict_time / index);
}

void calc_srv_redundancy_rate(_u64 index) {
//    {pc:{value:{thread:num}}}
//map<_u64, map<_u64, map<ThreadId,_u64 >>> srv_trace_map;
    for (auto stm_it = srv_trace_map.begin(); stm_it != srv_trace_map.end(); stm_it++) {
        cout << "PC:\t" << hex << stm_it->first << dec << endl;
        for (auto value_it = stm_it->second.begin(); value_it != stm_it->second.end(); value_it++) {

            _u64 cur_value_access_time = 0;
//            There's no need to clarify warps?
            for (auto &tid_it : value_it->second) {
                cur_value_access_time += tid_it.second;
            }
            cout << value_it->first << ":\t" << cur_value_access_time << "\t" << (double) cur_value_access_time / index
                 << endl;

        }
    }
}
/** This function can give us the representative value of every thread*/
void calc_vr_redundancy_rate(_u64 index) {
    map<ThreadId, _u64> thread_max_red_rate;
    _u64 sum_all_max_red = 0;
    // vertical redundancy:{thread: {pc: {value:times}}}
//    map<ThreadId, map<_u64, map<_u64, _u64 >>> vr_trace_map;
    for (auto & thread_it : vr_trace_map) {
        _u64 tmp_max = 0;
        for (auto & value_it : thread_it.second) {
//            If there's no one is over 1, not need to record.
            if (value_it.second > tmp_max && value_it.second > 1) {
                tmp_max = value_it.second;
//                also record the value?
            }
        }
        if (tmp_max > 0) {
            thread_max_red_rate.insert(pair<ThreadId, _u64>(thread_it.first, tmp_max));
            sum_all_max_red += tmp_max;
            cout << "thread";
            cout << thread_it.first << " redundancy " << tmp_max << endl;
        }
    }
    cout << "average redundancy rate:\t" << sum_all_max_red * 1.0 / index << endl;
}

void calc_hr_red_rate() {
    _u64 pc_nums = pcs.size();
    //{array:{value:num}}
//map<int, map<_u64, _u64>> hr_trace_map;
    for (auto &var_it : hr_trace_map) {
        _u64 max_acc_times = 0;
        tuple<long long, long long>max_acc_value;
        _u64 sum_times = 0;
        for (auto &value_it : var_it.second) {
            if (value_it.second > max_acc_times) {
                max_acc_times = value_it.second;
                max_acc_value = value_it.first;
            }
            sum_times += value_it.second;
        }
//        @todo output the distribution
        cout << "In array " << var_it.first << ", access value " << hex << get<0>(max_acc_value)<<"."<<get<1>(max_acc_value) << " " << dec << max_acc_times
             << " times" << endl;
        cout << "rate:" << max_acc_times * 1.0 / sum_times << endl;
    }
}

void analysis_hr_red_result() {
//    0: int 1: float
    char types[12] = {1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1,};

    for (auto &var_it : hr_trace_map) {
        _u64 max_acc_times = 0, max_acc_value = 0;
        _u64 sum_times = 0;
        float min_acc_value_f = 1000000.0;
        float max_acc_value_f = 0.0;
        int min_acc_value_i = INT32_MAX;
        int max_acc_value_i = INT32_MIN;

        ofstream out("array" + to_string(var_it.first) + ".csv");
        if (types[var_it.first] == 1) {
            for (auto &value_it : var_it.second) {
                float temp_value = store2float(value_it.first);
                for (int i = 0; i < value_it.second; ++i) {
                    out << temp_value << ",";
                }
//                out<<temp_value<<","<<value_it.second<<endl;
                if (temp_value > max_acc_value_f) {
//                max_acc_times = ;
                    max_acc_value_f = temp_value;
                } else {
                    if (temp_value < min_acc_value_f) {
                        min_acc_value_f = temp_value;
                    }
                }
//            sum_times += value_it.second;
            }
        } else {
            for (auto &value_it : var_it.second) {
                int temp_value = store2int(value_it.first);
                for (int i = 0; i < value_it.second; ++i) {
                    out << temp_value << ",";
                }
//                out<<temp_value<<","<<value_it.second<<endl;
                if (temp_value > max_acc_value_i) {
//                max_acc_times = ;
                    max_acc_value_i = temp_value;
                } else {
                    if (temp_value < min_acc_value_i) {
                        min_acc_value_i = temp_value;
                    }
                }
//            sum_times += value_it.second;
            }

        }
        out.close();
        if (types[var_it.first] == 1) {
            cout << "in array " << var_it.first << " value float range: [ " << min_acc_value_f << " , "
                 << max_acc_value_f
                 << " ] " << endl;
        } else {
            cout << "in array " << var_it.first << " value int range: [ " << min_acc_value_i << " , " << max_acc_value_i
                 << " ] " << endl;
        }

    }
}

/** Get the memory malloc information from hpctoolkit log file. At this moment, we only consider one CPU thread.
 * And the size is the number of bytes of the memory block. @todo check the item size in arrays. e.g. double type has 8 bytes while int only have 4 bytes.
 * */
void read_log_file(string input_file) {
    ifstream fin(input_file.c_str());
    std::istreambuf_iterator<char> beg(fin), end;
    string strdata(beg, end);
    fin.close();
    smatch sm;
    _u64 addr;
    int var_size;
    int i = 0;
    while (regex_search(strdata, sm, log_read_re)) {
        addr = stoull(sm[1], 0, 16);
        var_size = stoi(sm[2], 0, 10);
        vars_mem_block.emplace_back(pair<_u64, int>(addr, var_size));
        strdata = sm.suffix().str();
//        init arrays' index to avoid the empty check
        cout << "Memory alloc at " << hex << addr << " size " << dec << var_size << endl;
    }
}

//read input file and get every line
void read_input_file(string input_file, string target_name) {
    ifstream fin(input_file.c_str());
    string line;
//    just for trv's record of every redundancy
    _u64 index = 0;
    bool in_target_kernel = false;
    while (getline(fin, line)) {
        in_target_kernel = true;
        if (in_target_kernel) {

            smatch sm;
            regex_match(line, sm, line_read_re);
            if (sm.size() == 0) {
                cout << "This line can't match the regex:\t" << line << endl;
                continue;
            }

            _u64 pc, addr, value;
            int access_type;
            pc = stoull(sm[1], 0, 16);
            addr = stoull(sm[4], 0, 16);
            ThreadId tid = transform_tid(sm[2], sm[3]);
            threadid_max = get_max_threadId(tid, threadid_max);
            if (tid.bx == -1 || tid.by == -1 || tid.bz == -1 || tid.tx == -1 || tid.ty == -1 | tid.tz == -1) {
                cout << "Can not filter threadid from " << line << endl;
            }
            auto belongs = get_cur_addr_belong_index(addr);
            int belong = get<0>(belongs);
            _u64 offset = get<1>(belongs);
            switch (belong) {
                case -1 :
                    cout << "addr " << hex << addr << dec << " not found which array it belongs to" << endl;
                    break;
                case -2:
                    cout << "addr " << hex << addr << dec << " found over 1 array it belongs to" << endl;
                    break;
                default:
                    value = stoull(sm[5], 0, 16);
                    tuple<long long, long long> value_split;
                    switch (vars_type[belong]) {
                        case F32:
                            value_split = float2tuple(value, valid_float_digits);
                            break;
                        case S32:
                            value_split = make_tuple(value, 0);
                            break;
                        case F64:
                            break;
                        case S64:
                            break;
                        case U64:
                            break;
                        case U32:
                            break;
                        case S8:
                            break;
                        case U8:
                            break;
                    }
                    access_type = stoi(sm[6], 0, 16);
//                    get_tra_trace_map(tid, addr, access_type, belong);
//                    if (access_type == MEM_READ) {
//                        get_trv_r_trace_map(index, pc, tid, addr, value_split);
//                    } else {
//                        get_trv_w_trace_map(index, pc, tid, addr, value_split);
//                    }
//            get_srag_trace_map(index, pc, tid, addr, value);
//            get_srag_trace_map_test(index, pc, tid, addr, value);
//            get_srv_trace_map(pc, tid, addr, value);
//                    get_vr_trace_map(pc, tid, addr, value_split, vars_type[belong]);
            get_hr_trace_map(pc, tid, addr, value_split, belong);
//            get_dc_trace_map(pc, tid, addr, value);

            }
            index++;

        }
    }
//    cout << "tra rate\t" << calc_tra_redundancy_rate(index) << endl;

//    calc_trv_redundancy_rate(index);
//    calc_srag_redundancy_degree(index);
//    cout << "srag degree:";
//    for (int i = 0; i < 32; ++i) {
//        if (i % 4 == 0) {
//            cout << endl;
//        }
//        cout << i + 1 << ": " << srag_distribution[i] << '\t';
//    }
////    cout << "srag degree" << calc_srag_redundancy_degree_test(index) << endl;
//    auto ans_t = calc_sras_redundancy_rate(index);
//    cout << "sras conflict times:\t" << ans_t.first << endl << "sras conflict rate:\t" << ans_t.second << endl;

//    cout << "srv rate" << endl;
//    calc_srv_redundancy_rate(index);
//    cout << endl;

//    calc_vr_redundancy_rate(index);
//    calc_hr_red_rate();
//    filter_dead_copy();
//    analysis_hr_red_result();
}

int main(int argc, char *argv[]) {

    init();
    auto result = options.parse(argc, argv);
    read_log_file(result["log"].as<string>());
    read_input_file(result["input"].as<string>(), "");
    return 0;
}
