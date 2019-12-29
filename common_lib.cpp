//
// Created by find on 19-7-1.
//

#include "common_lib.h"


/**This function can calculate the reuse distance of every addr.
 * @arg acc_type : If the operation of this addr is write, the current reuse distance counter of this addr will be clear. 1 is read and 2 is write
 * @arg belong : It is the index of the array current addr belonging to.
 * I'm not sure whether we should change int to int8.*/
void get_tra_trace_map(ThreadId tid, _u64 addr, int acc_type, int belong, map<ThreadId, list<_u64 >> &tra_list,
                       map<_u64, vector<int >> &tra_trace_map, map<int, map<int, _u64 >> &tra_rd_dist) {
    map<ThreadId, list<_u64 >>::iterator tl_it;
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


double show_tra_redundancy(_u64 index, ThreadId &threadid_max, map<_u64, vector<int >> &tra_trace_map,
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
    cout << "reuse time rate:\t" << (double) r_num / index << endl;
// write the reuse distance histogram to csv files.
    for (int i = 0; i < tra_rd_dist.size(); ++i) {
        ofstream out("tra_" + to_string(i) + ".csv");
        auto tmp_rd_dist = tra_rd_dist[i];
        for (auto every_rd:tmp_rd_dist) {
            out << every_rd.first << "," << every_rd.second << endl;
        }
        out.close();
    }
    return tra_rate;

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
    cout << "silent_load rate\t" << (double) silent_load_num / line_num;
    cout << "silent_write_num\t" << silent_write_num << endl;
    cout << "silent_write rate\t" << (double) silent_write_num / line_num;
    cout << "dead_write_num\t" << dead_write_num << endl;
    cout << "dead_write rate\t" << (double) dead_write_num / line_num;
    ofstream out("trv_silent_load.csv");
    for (auto item : silent_load_pairs) {
        out << "< " << get<0>(item) << " , " << get<1>(item) << " >: " << hex << get<2>(item) << dec
            << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
        out.close();
    }
    ofstream out2("trv_silent_write.csv");
    for (auto item : silent_write_pairs) {
        out2 << "< " << get<0>(item) << " , " << get<1>(item) << " >: " << hex << get<2>(item) << dec
            << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
        out2.close();
    }
    ofstream out3("trv_dead_write.csv");
    for (auto item : dead_write_pairs) {
        out3 << "< " << get<0>(item) << " , " << get<1>(item) << " >: " << hex << get<2>(item) << dec
            << get<0>(get<3>(item)) << "." << get<1>(get<3>(item)) << endl;
        out3.close();
    }
}
