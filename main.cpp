#include "common_lib.h"
#include "cxxopts.hpp"

using cxxopts::Options;

void init();

ThreadId transform_tid(string bid, string tid);

void read_input_file(const string& input_file, string target_name);

ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max);

void get_srag_trace_map_test(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value);

void show_srag_redundancy();

double calc_srag_redundancy_degree_test(_u64 index);

// Spatial Redundancy Address Shared memory
void get_sras_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value);

pair<_u64, double> calc_sras_redundancy_rate(_u64 index);

// Spatial Redundancy Value
void get_srv_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value);

void calc_srv_redundancy_rate(_u64 index);

void filter_dead_copy();

// This block is the data definitions
// Every thread has a ordered set to save the read addresses.
map<ThreadId, list<_u64 >> tra_list;
// trace_map: {addr1 : [rd1, rd2,], }
map<_u64, vector<int >> tra_trace_map;
// {var:{rd1: 100, rd2:100}}.
// I'm not sure whether it is good to use the second int as key . Will the rd is over than int_max?
map<int, map<int, _u64 >> tra_rd_dist;
/**
Temporal Redundancy-Value dict_queue:{thread: {addr1: <value, pc, index>,}}
The last index variable is used to mark the offset in trace file which is used to detect dead store.
If val1 is float, it's better to use two numbers to represent the value. One is the integer part and the other one in decimal part
This map records a thread's last access.
*/
map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> trv_map_read;
map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> trv_map_write;
// save every pair: <pc1, pc2, addr, value>
vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> silent_load_pairs;
vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> silent_write_pairs;
vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> dead_write_pairs;
long long silent_load_num, dead_write_num, silent_write_num;
//Spatial Redundancy Address Global memory
// {pc: {tid: [(index, addr), ]}}
map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> srag_trace_map;
// {pc: {tid: {addr,}, ]}}
map<_u64, map<ThreadId, set<_u64>>> srag_trace_map_test;
//{pc:{value:{thread:num}}}
map<_u64, map<_u64, map<ThreadId, _u64 >>> srv_trace_map;
// Final histogram of global memory access distribution. index i means there is i+1 unique cache lines.
_u64 srag_distribution[WARP_SIZE];

// the sizes of thread block and grid
ThreadId threadid_max;
Options options("CUDA_RedShow", "A test suit for hpctoolkit santizer");
/** inter thread domain:
 * Check thread access value's similarity to guide approximate computing.
 * We should record all arrays' init state and final state of one thread.
 * srvbs: spatial redundancy value block similarity
 * {thread: {array_i: {offset: {first_value, last_value}}}}*/
map<ThreadId, map<int, map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>>>> srv_bs_trace_map;
// horizontal redundancy
// every array's memory start addr and size
vector<tuple<_u64, int>> vars_mem_block;
// the values' type of every array
//vector<BasicType> vars_type;
//@todo It's for test. We still need to figure out how to get the types of arrays
BasicType vars_type[12] = {F32, S32, S32, S32, S32, F32, F32, F32, F32, F32, F32, F32};
//{var:{value:counter}}
map<int, map<tuple<long long, long long>, _u64>> hr_trace_map;
// {pc: { var:{value: counter} }}
map<_u64, map<int, map<tuple<long long, long long>, _u64 >>> hr_trace_map_pc_dist;
// to get the number of pcs
set<_u64> pcs;
//at this time, I use vector as the main format to store tracemap, but if we can get an input of array size, it can be changed to normal array.
// {var:{index1,index2}}
map<int, set<_u64 >> dc_trace_map;
// We will ignore the digits after this index in decimal part.
int valid_float_digits = 5;


regex line_read_re(R"(0x(.+?)\|\((.+?)\)\|\((.+?)\)\|0x(.+?)\|0x(.+)\|(.+))");

//Allocate memory address 0x7fe39b600000, size 40
//used to filter memory allocation information
regex log_read_re("Allocate memory address 0x(.+), size (\\d+)");

// execute params
//
vector<int> execute_arg;

ostream &operator<<(ostream &out, const ThreadId &A) {
    out << "(" << A.bx << "," << A.by << "," << A.bz << ")(" << A.tx << "," << A.ty << "," << A.tz << ")";
    return out;
}

void init() {
    silent_load_num = 0;
    dead_write_num = 0;
    options.add_options()
            ("i,input", "Input trace file", cxxopts::value<std::string>())
            ("l,log", "Input log file", cxxopts::value<std::string>());
    memset(srag_distribution, 0, sizeof(_u64) * 32);
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
//
//void get_sras_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, _u64 value) {
////    tuple: <_u64, _u64, _u64>(index, addr, value)
//    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
//    stm_it = srag_trace_map.find(pc);
//    if (stm_it == srag_trace_map.end()) {
//        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>> tmp = {{tid, vector<tuple<_u64, _u64, _u64 >>{
//                tuple<_u64, _u64, _u64>(index, addr, value)},}};
//        srag_trace_map.insert(pair<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64>>>>(pc, tmp));
//    } else {
//        map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>::iterator m_it;
//        m_it = stm_it->second.find(tid);
////      stm has addr key but inner doesn't have tid key
//        if (m_it == stm_it->second.end()) {
//            vector<tuple<_u64, _u64, _u64 >> tmp_1 = {tuple<_u64, _u64, _u64>(index, addr, value)};
//            stm_it->second.insert(pair<ThreadId, vector<tuple<_u64, _u64, _u64 >>>(tid, tmp_1));
//        } else {
//            m_it->second.emplace_back(tuple<_u64, _u64, _u64>(index, addr, value));
//        }
//
//    }
//
//}
// commented by FindHao. This function is for the vertical red. We first talk about it in 9.20.
//void get_vr_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value) {
//    auto vrtm_it = srv_bs_trace_map.find(tid);
//    if (vrtm_it == srv_bs_trace_map.end()) {
//        map<_u64, map<_u64, _u64 >> tmp = {{pc, map<_u64, _u64>{pair<_u64, _u64>(value, 1)}}};
//        srv_bs_trace_map.insert(pair<ThreadId, map<_u64, map<_u64, _u64>>>(tid, tmp));
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

//void get_vr_trace_map(_u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value,
//                      BasicType acc_type) {
//    auto vrtm_it = srv_bs_trace_map.find(tid);
//    if (vrtm_it == srv_bs_trace_map.end()) {
//        map<tuple<long long, long long>, _u64> tmp_1 = {pair<tuple<long long, long long>, _u64>(value, 1)};
//        srv_bs_trace_map.insert(pair<ThreadId, map<tuple<long long, long long>, _u64>>(tid, tmp_1));
//    } else {
//        auto v_it = vrtm_it->second.find(value);
//        if (v_it == vrtm_it->second.end()) {
//            vrtm_it->second.insert(pair<tuple<long long, long long>, _u64>(value, 1));
//        } else {
//            vrtm_it->second[value] += 1;
//        }
//    }
//}

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


void filter_dead_copy() {
    // {var:{index1,index2}}
    for (auto dc_it = dc_trace_map.begin(); dc_it != dc_trace_map.end(); ++dc_it) {
        cout << "The array " << dc_it->first << " use rate: " << dc_it->second.size() * 1.0 << " / "
             << (*dc_it->second.rbegin() + 1)
             << " = " << dc_it->second.size() * 1.0 / (*dc_it->second.rbegin() + 1) << endl;
    }
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
//
//pair<_u64, double> calc_sras_redundancy_rate(_u64 index) {
//    _u64 conflict_time = 0;
//    map<_u64, map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>>::iterator stm_it;
//    int bz, by, bx, tz, ty, tx;
//    for (stm_it = srag_trace_map.begin(); stm_it != srag_trace_map.end(); stm_it++) {
//        for (bz = 0; bz <= threadid_max.bz; bz++) {
//            for (by = 0; by <= threadid_max.by; ++by) {
//                for (bx = 0; bx <= threadid_max.bx; ++bx) {
//                    for (tz = 0; tz <= threadid_max.tz; ++tz) {
//                        for (ty = 0; ty <= threadid_max.ty; ++ty) {
//                            for (tx = 0; tx <= threadid_max.tx; tx += 32) {
//
////                              The max number of remain items in threads' vectors. At this moment, every pc only has one access per thread. So actually, remain_items seems equal to 1.
//                                int remain_items = 0;
//                                for (int tx_i = 0; tx_i < 32; ++tx_i) {
//                                    ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
////                                    m_it: map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>
//                                    auto m_it = stm_it->second.find(tmp_id);
//                                    if (m_it != stm_it->second.end()) {
//                                        remain_items = max(remain_items, (int) m_it->second.size());
//                                    }
//                                }
////                                if (remain_items > 1) { cout << "remain_items is " << remain_items << endl; }
//                                for (int i = 0; i < remain_items; ++i) {
//                                    int this_iterations_valid_item = 32;
////                                    per warp, per pc, per iteration of a loop
//                                    set<_u64> bank_visit;
//                                    for (int tx_i = 0; tx_i < 32; ++tx_i) {
//                                        ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                        auto m_it = stm_it->second.find(tmp_id);
//                                        if (m_it != stm_it->second.end() && m_it->second.size() > i) {
////                                            the tuple has three items now. We need the second one, addr
//                                            bank_visit.insert(get<1>(m_it->second[i]));
//                                        } else {
////                                            Not all threads in this warp works in this iteration.
//                                            this_iterations_valid_item--;
//                                        }
//                                    }
////                                    How many conflicts
//                                    conflict_time += this_iterations_valid_item - bank_visit.size();
//                                }
//                            }
//                        }
//                    }
//                }
//            }
//        }
//    }
//    return make_pair(conflict_time, (double) conflict_time / index);
//}

void calc_srv_redundancy_rate(_u64 index) {
//    {pc:{value:{thread:num}}}
//map<_u64, map<_u64, map<ThreadId,_u64 >>> srv_trace_map;
    for (auto &stm_it : srv_trace_map) {
        cout << "PC:\t" << hex << stm_it.first << dec << endl;
        for (auto & value_it : stm_it.second) {

            _u64 cur_value_access_time = 0;
//            There's no need to clarify warps?
            for (auto &tid_it : value_it.second) {
                cur_value_access_time += tid_it.second;
            }
            cout << value_it.first << ":\t" << cur_value_access_time << "\t" << (double) cur_value_access_time / index
                 << endl;

        }
    }
}

/** This function can give us the representative value of every thread*/
//void calc_vr_redundancy_rate(_u64 index) {
//    map<ThreadId, _u64> thread_max_red_rate;
//    _u64 sum_all_max_red = 0;
//    // vertical redundancy:{thread: {pc: {value:times}}}
////    map<ThreadId, map<_u64, map<_u64, _u64 >>> srv_bs_trace_map;
//    for (auto &thread_it : srv_bs_trace_map) {
//        _u64 tmp_max = 0;
//        for (auto &value_it : thread_it.second) {
////            If there's no one is over 1, not need to record.
//            if (value_it.second > tmp_max && value_it.second > 1) {
//                tmp_max = value_it.second;
////                also record the value?
//            }
//        }
//        if (tmp_max > 0) {
//            thread_max_red_rate.insert(pair<ThreadId, _u64>(thread_it.first, tmp_max));
//            sum_all_max_red += tmp_max;
//            cout << "thread";
//            cout << thread_it.first << " redundancy " << tmp_max << endl;
//        }
//    }
//    cout << "average redundancy rate:\t" << sum_all_max_red * 1.0 / index << endl;
//}

void calc_hr_red_rate() {
    _u64 pc_nums = pcs.size();
    //{array:{value:num}}
//    map<int, map<tuple<long long, long long>, _u64>> hr_trace_map;
    for (auto &var_it : hr_trace_map) {
        _u64 max_acc_times = 0;
        tuple<long long, long long> max_acc_value;
        _u64 sum_times = 0;
        for (auto &value_it : var_it.second) {
            if (value_it.second > max_acc_times) {
                max_acc_times = value_it.second;
                max_acc_value = value_it.first;
            }
            sum_times += value_it.second;
        }
        cout << "In array " << var_it.first << ", access value " << get<0>(max_acc_value) << "."
             << get<1>(max_acc_value) << " " << max_acc_times << " times" << endl;
        cout << "rate:" << max_acc_times * 1.0 / sum_times << endl;
//        write value distribution to log files
        ofstream out("hr_" + to_string(var_it.first) + ".csv");
        for (auto &item : var_it.second) {
            out << get<0>(item.first) << "." << get<1>(item.first) << "," << item.second << endl;
        }
        out.close();
    }
}


/** Get the memory malloc information from hpctoolkit log file. At this moment, we only consider one CPU thread.
 * And the size is the number of bytes of the memory block. @todo check the item size in arrays. e.g. double type has 8 bytes while int only have 4 bytes.
 * */
void read_log_file(const string& input_file) {
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
void read_input_file(const string& input_file) {
    ifstream fin(input_file.c_str());
    string line;
//    just for trv's record of every redundancy
    _u64 index = 0;
    while (getline(fin, line)) {
        smatch sm;
        regex_match(line, sm, line_read_re);
        if (sm.empty()) {
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
        auto belongs = get_cur_addr_belong_index(addr, vars_mem_block);
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
                        value_split = float2tuple(store2float(value), valid_float_digits);
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


                get_tra_trace_map(tid, addr, access_type, belong, tra_list, tra_trace_map, tra_rd_dist);
                if (access_type == MEM_READ) {
                    get_trv_r_trace_map(index, pc, tid, addr, value_split, trv_map_read, silent_load_num,
                                        silent_load_pairs);
                } else {
                    get_trv_w_trace_map(index, pc, tid, addr, value_split, trv_map_write,
                                        silent_write_num, silent_write_pairs,
                                        trv_map_read, dead_write_num, dead_write_pairs);
                }
                get_srag_trace_map(index, pc, tid, addr, srag_trace_map);
//            get_srag_trace_map_test(index, pc, tid, addr, value);
//            get_srv_trace_map(pc, tid, addr, value);
//                    get_vr_trace_map(pc, tid, addr, value_split, vars_type[belong]);
                get_hr_trace_map(pc, tid, addr, value_split, belong, pcs, hr_trace_map, hr_trace_map_pc_dist);
//                get_dc_trace_map(pc, tid, addr, value);

        }
        index++;
    }
    cout << "tra rate\t" << show_tra_redundancy(index, threadid_max, tra_trace_map, tra_rd_dist) << endl;

    show_trv_redundancy_rate(index, silent_load_num, silent_load_pairs, silent_write_num, silent_write_pairs,
                             dead_write_num, dead_write_pairs);
    show_srag_redundancy(srag_trace_map, threadid_max, srag_distribution);
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
//    show_hr_redundancy();
}

int main(int argc, char *argv[]) {

    init();
    auto result = options.parse(argc, argv);
    execute_arg = result["args"].as<vector<int>>();
    read_log_file(result["log"].as<string>());
    read_input_file(result["input"].as<string>());
    return 0;
}
