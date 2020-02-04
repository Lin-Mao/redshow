//
// Created by find on 19-7-1.
//

#include "common_lib.h"


regex tid_re(R"((\d+),(\d+),(\d+))");


/**This function can calculate the reuse distance of every addr.
 * @arg acc_type : If the operation of this addr is write, the current reuse distance counter of this addr will be clear. 1 is read and 2 is write
 * @arg belong : It is the index of the array current addr belonging to.
 * I'm not sure whether we should change int to int8.*/
void get_tra_trace_map(const ThreadId tid, _u64 addr, int acc_type, int belong, map<ThreadId, list<_u64 >> &tra_list,
                       map<_u64, vector<int >> &tra_trace_map, map<int, map<int, _u64 >> &tra_rd_dist) {
    map<ThreadId, list<_u64 >>::iterator tl_it;
//
    tl_it = tra_list.find(tid);
//  A new thread occurs. We didn't see this thread before.
    if (tl_it == tra_list.end()) {
        list<_u64> tmp;
        tmp.push_back(addr);
        tra_list[tid] = tmp;
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
                tra_trace_map[addr].push_back(tmp_rd);
//                 update the log of rd distributions
                tra_rd_dist[belong][tmp_rd] += 1;
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


void show_tra_redundancy(_u64 index, ThreadId &threadid_max, map<_u64, vector<int >> &tra_trace_map,
                         map<int, map<int, _u64 >> &tra_rd_dist) {
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
    cout << "reuse rate:\t" << (double) r_num / index << endl;
    cout << "tra rate:\t" << tra_rate << endl;
// write the reuse distance histogram to csv files.
    for (int i = 0; i < tra_rd_dist.size(); ++i) {
        ofstream out("tra_" + to_string(i) + ".csv");
        auto tmp_rd_dist = tra_rd_dist[i];
        for (auto every_rd:tmp_rd_dist) {
            out << every_rd.first << "," << every_rd.second << endl;
        }
        out.close();
    }

}


/**@arg index: It is used to mark the silent load offset in the trace file. It is also used to mark the last read which is used to */
void get_trv_r_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> &value,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_read,
                         long long &silent_load_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_load_pairs) {
    auto tmr_it = trv_map_read.find(tid);
    map<_u64, tuple<tuple<long long, long long>, _u64, _u64 >> record;
//    record current operation.
    record[addr] = make_tuple(value, pc, index);
//    The trv_map_read doesn't have the thread's record
    if (tmr_it == trv_map_read.end()) {
        trv_map_read[tid] = record;
    } else {
        auto m_it = tmr_it->second.find(addr);
//      The trv_map_read's thread record doesn't have the current addr record.
//      m_it format: <value_high, value_low, last_pc>
        if (m_it == tmr_it->second.end()) {
            tmr_it->second[addr] = record[addr];
        } else {
            if (get<0>(m_it->second) == value) {
                silent_load_num++;
                silent_load_pairs.emplace_back(get<1>(m_it->second), pc, addr, value);
            }
            m_it->second = record[addr];
        }
    }
}

/**@arg index: It is used to mark the silent write and dead write offset in the trace file. */
void get_trv_w_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_write,
                         long long &silent_write_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_write_pairs,
                         map<ThreadId, map<_u64, tuple<tuple<long long, long long>, _u64, _u64>>> &trv_map_read,
                         long long &dead_write_num,
                         vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &dead_write_pairs) {
    auto tmw_it = trv_map_write.find(tid);
//    <addr, <<value_high, value_low>, pc, index>
    map<_u64, tuple<tuple<long long, long long>, _u64, _u64 >> record;
    record[addr] = make_tuple(value, pc, index);
    if (tmw_it == trv_map_write.end()) {
        trv_map_write[tid] = record;
    } else {

        auto m_it = tmw_it->second.find(addr);
        if (m_it == tmw_it->second.end()) {
            tmw_it->second[addr] = record[addr];
        } else {
            if (get<0>(m_it->second) == value) {
                silent_write_num++;
                silent_write_pairs.emplace_back(get<1>(m_it->second), pc, addr, value);
            }
//            check the last read about this <thread, addr>
            auto tmr_it = trv_map_read.find(tid);
            if (tmr_it != trv_map_read.end()) {
                auto m_it2 = tmr_it->second.find(addr);
                if (m_it2 != tmr_it->second.end()) {
//                    tuple < tuple<long long, long long>, _u64 , _u64> the last one is index of this read.
                    auto r_value = m_it2->second;
//                  the last read of this addr is earlier than last write.
                    if (get<2>(r_value) < get<2>(m_it->second)) {
                        dead_write_num++;
                        dead_write_pairs.emplace_back(get<1>(m_it->second), pc, addr, value);
                    }
                }
            }
            m_it->second = record[addr];
        }
    }
}

//
void show_trv_redundancy_rate(_u64 line_num, long long &silent_load_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_load_pairs,
                              long long &silent_write_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &silent_write_pairs,
                              long long &dead_write_num,
                              vector<tuple<_u64, _u64, _u64, tuple<long long, long long>>> &dead_write_pairs) {
    if (line_num == 0) {
        cout << "No memory operation found" << endl;
        return;
    }
    cout << "silent_load_num\t" << silent_load_num << endl;
    cout << "silent_load rate\t" << (double) silent_load_num / line_num << endl;
    cout << "silent_write_num\t" << silent_write_num << endl;
    cout << "silent_write rate\t" << (double) silent_write_num / line_num << endl;
    cout << "dead_write_num\t" << dead_write_num << endl;
    cout << "dead_write rate\t" << (double) dead_write_num / line_num << endl;
    ofstream out("trv_silent_load.csv");
    for (auto item : silent_load_pairs) {
//        0:pc1, 1: pc2, 2: addr,
        out << get<0>(item) << " , " << get<1>(item) << " , " << hex << get<2>(item) << " , " << dec
            << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
    }
    out.close();
    ofstream out2("trv_silent_write.csv");
    for (auto item : silent_write_pairs) {
//        0:pc1, 1: pc2, 2: addr,
        out2 << get<0>(item) << " , " << get<1>(item) << " , " << hex << get<2>(item) << " , " << dec
             << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
    }
    out2.close();
    ofstream out3("trv_dead_write.csv");
    for (auto item : dead_write_pairs) {
        out3 << get<0>(item) << " , " << get<1>(item) << " , " << hex << get<2>(item) << " , " << dec
             << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
    }
    out3.close();
}

/**@arg: index, if there are loops in original code, every pc will own lot of access in same thread.
 * The index is similar to timestamp to clarify which iteration the current access in.
 * srag_trace_map: {pc: {tid: [(index, addr), ]}}*/
void get_srag_trace_map(_u64 index, _u64 pc, ThreadId tid, _u64 addr,
                        map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map) {
//    stm_it->second is a tuple: <_u64, _u64, _u64>(index, addr, value)
    auto stm_it = srag_trace_map.find(pc);
    if (stm_it == srag_trace_map.end()) {
        map<ThreadId, vector<tuple<_u64, _u64>>> tmp;
        tmp[tid] = vector<tuple<_u64, _u64>>{
                make_tuple(index, addr)};
        srag_trace_map[pc] = tmp;
    } else {
        map<ThreadId, vector<tuple<_u64, _u64 >>>::iterator m_it;
        m_it = stm_it->second.find(tid);
//      stm has addr key but inner doesn't have tid key
        if (m_it == stm_it->second.end()) {
            vector<tuple<_u64, _u64>> tmp_1 = {make_tuple(index, addr)};
            stm_it->second[tid] = tmp_1;
        } else {
            m_it->second.emplace_back(make_tuple(index, addr));
        }
    }
}

// {pc: {tid: {addr,}, ]}}
void show_srag_redundancy(map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> &srag_trace_map, ThreadId &threadid_max,
                          _u64 (&srag_distribution)[WARP_SIZE]) {
//    _u64 all_transactions = 0;
    int bz, by, bx, tz, ty, tx;
    for (auto stm_it:srag_trace_map) {
        for (bz = 0; bz <= threadid_max.bz; bz++) {
            for (by = 0; by <= threadid_max.by; ++by) {
                for (bx = 0; bx <= threadid_max.bx; ++bx) {
                    for (tz = 0; tz <= threadid_max.tz; ++tz) {
                        for (ty = 0; ty <= threadid_max.ty; ++ty) {
                            for (tx = 0; tx <= threadid_max.tx; tx += 32) {
                                int remain_items = 0;
//                                Find how many iterations the thread could have.
                                for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                    ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
//                                    m_it: map<ThreadId, vector<tuple<_u64, _u64, _u64 >>>
                                    auto m_it = stm_it.second.find(tmp_id);
                                    if (m_it != stm_it.second.end()) {
                                        remain_items = max(remain_items, (int) m_it->second.size());
                                    }
                                }
                                for (int i = 0; i < remain_items; ++i) {
//                                    per warp, per pc, per iteration of a loop
                                    set<_u64> warp_unique_cache_lines;
                                    for (int tx_i = 0; tx_i < 32; ++tx_i) {
                                        ThreadId tmp_id = {bx, by, bz, tx + tx_i, ty, tz};
                                        auto m_it = stm_it.second.find(tmp_id);
                                        if (m_it != stm_it.second.end() && m_it->second.size() > i) {
//                                            the tuple has three items now. We need the second one, addr
//                                      Every cache line is 32bytes now.
                                            int x = get<1>(m_it->second[i]) >> 5;
                                            warp_unique_cache_lines.insert(
                                                    get<1>(m_it->second[tx_i]) >> CACHE_LINE_BYTES_BIN);
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

// {thread: {array_i: {first_value, last_value} }}
void get_srv_bs_trace_map(ThreadId tid, tuple<long long, long long> value, int belong, int offset,
                          map<ThreadId, map<int, map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>>>> &srv_bs_trace_map) {
    auto tm_id = srv_bs_trace_map.find(tid);
    if (tm_id == srv_bs_trace_map.end()) {
//        array_i:
        map<int, map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>>> temp;
//        offset:
        map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>> temp2;
        temp2[offset] = make_tuple(value, value);
        temp[belong] = temp2;
        srv_bs_trace_map[tid] = temp;
    } else {
//        if the trace map has record, the first_value must be not empty. So we can just update last_value
        auto belong_id = tm_id->second.find(belong);
        if (belong_id == tm_id->second.end()) {
//          offset:
            map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>> temp2;
            temp2[offset] = make_tuple(value, value);
            tm_id->second[belong] = temp2;
        } else {
            belong_id->second[offset] = make_tuple(get<0>(belong_id->second[offset]), value);
        }
    }
}

void show_srv_bs_redundancy(ThreadId &threadid_max,
                            map<ThreadId, map<int, map<int, tuple<tuple<long long, long long>, tuple<long long, long long>>>>> &srv_bs_trace_map) {

}


void get_hr_trace_map(_u64 pc, ThreadId tid, _u64 addr, tuple<long long, long long> value, int belong, set<_u64> &pcs,
                      map<int, map<tuple<long long, long long>, _u64>> &hr_trace_map,
                      map<_u64, map<int, map<tuple<long long, long long>, _u64 >>> &hr_trace_map_pc_dist) {
    pcs.insert(pc);
//   //{var:{value:num}} map<int, map<_u64, _u64>> hr_trace_map;
    hr_trace_map[belong][value] += 1;
//  {pc: { var:{value: num} }}
    hr_trace_map_pc_dist[pc][belong][value] += 1;
}

void show_hr_redundancy(map<int, map<tuple<long long, long long>, _u64>> &hr_trace_map, set<_u64> &pcs) {
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
             << get<1>(max_acc_value) << " " << max_acc_times << " times, ";
        cout << "rate:" << max_acc_times * 1.0 / sum_times << endl;
//        write value distribution to log files
        ofstream out("hr_" + to_string(var_it.first) + ".csv");
        for (auto &item : var_it.second) {
            out << get<0>(item.first) << "." << get<1>(item.first) << "," << item.second << endl;
        }
        out.close();
    }
}

/**e.g. a[100] belongs to array a
 * @return -1 not found
 * @return -2 find over 1 array it belongs
 * */
int get_cur_addr_belong(_u64 addr, vector<tuple<_u64, int>> &vars_mem_block) {
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
tuple<int, _u64> get_cur_addr_belong_index(_u64 addr, vector<tuple<_u64, int>> &vars_mem_block) {
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

// dead copy means copy the array but some part of this array doesn't use
void get_dc_trace_map(_u64 pc, ThreadId tid, _u64 addr, _u64 value, vector<tuple<_u64, int>> &vars_mem_block,
                      map<int, set<_u64 >> &dc_trace_map) {
    auto belongs = get_cur_addr_belong_index(addr, vars_mem_block);
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

bool ThreadId::operator<(const ThreadId &o) const {
    return (bz < o.bz) || (bz == o.bz && by < o.by) || (bz == o.bz && by == o.by && bx < o.bx) ||
           (bz == o.bz && by == o.by && bx == o.bx && tz < o.tz) ||
           (bz == o.bz && by == o.by && bx == o.bx && tz == o.tz && ty < o.ty) ||
           (bz == o.bz && by == o.by && bx == o.bx && tz == o.tz && ty == o.ty && tx < o.tx);
}

bool ThreadId::operator==(const ThreadId &o) const {
    return (bz == o.bz) && (by == o.by) && (bx == o.bx) && (tz == o.tz) && (ty == o.ty) && (tx == o.tx);
}


