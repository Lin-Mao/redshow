#include "common_lib.h"
#include "cxxopts.hpp"

using cxxopts::Options;

void init();

ThreadId transform_tid(string bid, string tid);

void read_input_file(const string &input_file, string target_name);

ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max);

// This block is the data definitions
// Every thread has a ordered set to save the read addresses.
map<ThreadId, list<_u64 >> tra_list;
// trace_map: {addr1 : [rd1, rd2,], }
map<_u64, vector<int >> tra_trace_map;
// {var:{rd1: 100, rd2:100}}. For different array.
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
BasicType vars_type[12] = {F32, S32, F32, F32,F32,F32,F32, F32, F32, F32, F32, F32};
//{var:{value:counter}}
map<int, map<tuple<long long, long long>, _u64>> hr_trace_map;
// {pc: { var:{value: counter} }}
map<_u64, map<int, map<tuple<long long, long long>, _u64 >>> hr_trace_map_pc_dist;
// to get the number of pcs
set<_u64> pcs;
//at this time, I use vector as the main format to store tracemap, but if we can get an input of array size, it can be changed to normal array.
// {var:{index1,index2}}
map<int, set<_u64 >> dc_trace_map;
// We will change the bits after this index to 0. F32:23, F64:52
int valid_float_digits = 18;
int valid_double_digits = 40;

regex line_read_re(R"(0x(.+?)\|\((.+?)\)\|\((.+?)\)\|0x(.+?)\|0x(.+)\|(.+))");

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
            ("i,input", "Input trace file", cxxopts::value<std::string>())
            ("l,log", "Input log file", cxxopts::value<std::string>());
    memset(srag_distribution, 0, sizeof(_u64) * 32);
}

void filter_dead_copy() {
    // {var:{index1,index2}}
    for (auto dc_it = dc_trace_map.begin(); dc_it != dc_trace_map.end(); ++dc_it) {
        cout << "The array " << dc_it->first << " use rate: " << dc_it->second.size() * 1.0 << " / "
             << (*dc_it->second.rbegin() + 1)
             << " = " << dc_it->second.size() * 1.0 / (*dc_it->second.rbegin() + 1) << endl;
    }
}

/** Get the memory malloc information from hpctoolkit log file. At this moment, we only consider one CPU thread.
 * And the size is the number of bytes of the memory block. @todo check the item size in arrays. e.g. double type has 8 bytes while int only have 4 bytes.
 * */
void read_log_file(const string &input_file) {
    ifstream fin(input_file.c_str());
    if(fin.fail()){
        cout<<"Error when opening file "<<input_file<<endl;
        return;
    }
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
        cout << "Array " << i << " start at " << hex << addr << dec << " , size " << var_size << endl;
        i++;
    }
}

//read input file and get every line
void read_input_file(const string &input_file) {
    ifstream fin(input_file.c_str());
    if(fin.fail()){
        cout<<"Error when opening file "<<input_file<<endl;
        return;
    }
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
//                @todo vectorized access
                if (sm[5].length() > 8) {
                    value = stoull(sm[5].str().substr(0, 8), 0, 1 - 6);
                } else {
                    value = stoull(sm[5], 0, 16);
                }
                tuple<long long, long long> value_split;
                switch (vars_type[belong]) {
                    case F32:
                         value_split = float2tuple(store2float(value), valid_float_digits);
                        break;
                    case S32:
                        value_split = make_tuple(store2int(value), 0);
                        break;
                    case F64:
                        break;
                    case S64:
                        break;
                    case U64:
                        break;
                    case U32:
                        value_split = make_tuple(store2uint(value), 0);
                        break;
                    case S8:
                        if (sm[5].length() > 2) {
                            cout << "a 8-bit value is more than 8-bit\t" << hex << sm[5] << dec;
                        }
                        value_split = make_tuple(store2char(value), 0);
                        break;
                    case U8:
                        if (sm[5].length() > 2) {
                            cout << "a unsigned 8-bit value is more than 8-bit\t" << hex << sm[5] << dec;
                        }
                        value_split = make_tuple(store2uchar(value), 0);
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
    show_tra_redundancy(index, threadid_max, tra_trace_map, tra_rd_dist);

    show_trv_redundancy_rate(index, silent_load_num, silent_load_pairs, silent_write_num, silent_write_pairs,
                             dead_write_num, dead_write_pairs);
    show_srag_redundancy(srag_trace_map, threadid_max, srag_distribution);
//    calc_vr_redundancy_rate(index);
//    filter_dead_copy();
    show_hr_redundancy(hr_trace_map, pcs);
}

int main(int argc, char *argv[]) {

    init();
    auto result = options.parse(argc, argv);
    read_log_file(result["log"].as<string>());
    read_input_file(result["input"].as<string>());
    return 0;
}
