#include "common_lib.h"
#include "cxxopts.hpp"

using cxxopts::Options;

void init();

ThreadId transform_tid(string bid, string tid);

void read_input_file(const string &input_file, string target_name);

ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max);

ostream &operator<<(ostream &out, const ThreadId &A) {
  out << "(" << A.bx << "," << A.by << "," << A.bz << ")(" << A.tx << "," << A.ty << "," << A.tz << ")";
  return out;
}

extern map<ThreadId, list<_u64 >> tra_list;
extern map<_u64, vector<int >> tra_trace_map;
extern map<int, map<int, _u64 >> tra_rd_dist;
extern map<ThreadId, map<_u64, tuple<_u64, _u64, _u64>>> trv_map_read;
extern map<ThreadId, map<_u64, tuple<_u64, _u64, _u64>>> trv_map_write;
extern vector<tuple<_u64, _u64, _u64, _u64, BasicType>> silent_load_pairs;
extern vector<tuple<_u64, _u64, _u64, _u64, BasicType>> silent_write_pairs;
extern vector<tuple<_u64, _u64, _u64, _u64, BasicType>> dead_write_pairs;
extern long long silent_load_num, dead_write_num, silent_write_num;
extern map<_u64, map<ThreadId, vector<tuple<_u64, _u64>>>> srag_trace_map;
extern map<_u64, map<ThreadId, set<_u64>>> srag_trace_map_test;
extern map<_u64, map<_u64, map<ThreadId, _u64 >>> srv_trace_map;
extern _u64 srag_distribution[WARP_SIZE];
extern map<ThreadId, map<int, map<int, long long>>> srv_bs_trace_map;
extern map<tuple<_u64, AccessType>, map<_u64, _u64 >> hr_trace_map;
extern set<_u64> pcs;
extern map<int, set<_u64 >> dc_trace_map;


Options options("CUDA_RedShow", "A test suit for hpctoolkit santizer");
// every array's memory start addr and size
vector<tuple<_u64, int>> vars_mem_block;
// the sizes of thread block and grid
ThreadId threadid_max;
map<BasicType, int> type_length;
// the values' type of every array
//@todo It's for test. We still need to figure out how to get the types of arrays
BasicType vars_type[12] = {F32, S32, F32, F32, F32, F32, F32, F32, F32, F32, F32, F32};

void init() {
  options.add_options()
      ("i,input", "Input trace file", cxxopts::value<std::string>())
      ("l,log", "Input log file", cxxopts::value<std::string>());
  type_length = {{F32, 8},
                 {F64, 16},
                 {S64, 16},
                 {U64, 16},
                 {S32, 8},
                 {U32, 8},
                 {S8,  2},
                 {U8,  2}};
}


//void filter_dead_copy() {
//  // {var:{index1,index2}}
//  for (auto dc_it = dc_trace_map.begin(); dc_it != dc_trace_map.end(); ++dc_it) {
//    cout << "The array " << dc_it->first << " use rate: " << dc_it->second.size() * 1.0 << " / "
//         << (*dc_it->second.rbegin() + 1)
//         << " = " << dc_it->second.size() * 1.0 / (*dc_it->second.rbegin() + 1) << endl;
//  }
//}

/** Get the memory malloc information from hpctoolkit log file. At this moment, we only consider one CPU thread.
 * And the size is the number of bytes of the memory block. @todo check the item size in arrays. e.g. double type has 8 bytes while int only have 4 bytes.
 * */
void read_log_file(const string &input_file) {
  //Allocate memory address 0x7fe39b600000, size 40
//used to filter memory allocation information
  regex log_read_re("Allocate memory address 0x(.+), size (\\d+)");
  ifstream fin(input_file.c_str());
  if (fin.fail()) {
    cout << "Error when opening file " << input_file << endl;
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
  regex line_read_re(R"(0x(.+?)\|\((.+?)\)\|\((.+?)\)\|0x(.+?)\|0x(.+)\|(.+))");
  ifstream fin(input_file.c_str());
  if (fin.fail()) {
    cout << "Error when opening file " << input_file << endl;
    return;
  }
  static std::map<uint64_t, AccessType> array_type;
  int i = 0;
  for (auto var:vars_type) {
    AccessType atype;
    atype.type = AccessType::FLOAT;
    atype.unit_size = 32;
    atype.vec_size = 32;
    array_type[i++] = atype;
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
    _u64 pc, addr, value_hex;
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
        int t_len = type_length[vars_type[belong]];
        for (int i = 0; i < sm[5].str().length() / t_len; ++i) {
//                    @todo check the stoull's 2th argument
          int left_index = i * t_len;
//          int right_index = i * t_len + t_len - 1;
          value_hex = stoull(sm[5].str().substr(left_index, t_len), nullptr, 16);
          _u64 value = INT64_MAX;
          switch (vars_type[belong]) {
            case F32:
              value = store2float(value_hex, valid_float_digits);
              break;
            case S32:
            case U32:
              value = store2uint(value_hex);
              break;
            case F64:
              value = store2double(value_hex, valid_double_digits);
              break;
            case S64:
            case U64:
              value = store2u64(value_hex);
              break;
            case S8:
            case U8:
              value = store2uchar(value_hex);
              break;
            default:
              cout << "Error: There is some string can not be parsed." << line << endl;
          }

          access_type = stoi(sm[6], 0, 16);

//          get_tra_trace_map(tid, addr, access_type, belong, tra_list, tra_trace_map, tra_rd_dist);
//          if (access_type == MEM_READ) {
//            get_trv_r_trace_map(index, pc, tid, addr, value, trv_map_read, silent_load_num,
//                                silent_load_pairs, vars_type[belong]);
//          } else {
//            get_trv_w_trace_map(index, pc, tid, addr, value, trv_map_write,
//                                silent_write_num, silent_write_pairs,
//                                trv_map_read, dead_write_num, dead_write_pairs, vars_type[belong]);
//          }
//          get_srag_trace_map(index, pc, tid, addr, srag_trace_map);
//            get_srag_trace_map_test(index, pc, tid, addr, value_hex);
//            get_srv_trace_map(pc, tid, addr, value_hex);
//                    get_vr_trace_map(pc, tid, addr, value_split, vars_type[belong]);

          get_hr_trace_map(value, (_u64) belong, array_type[belong], hr_trace_map);
//                get_dc_trace_map(pc, tid, addr, value_hex);
          index++;
        }

    }


  }
//  show_tra_redundancy(index, threadid_max, tra_trace_map, tra_rd_dist);
//
//  show_trv_redundancy_rate(index, silent_load_num, silent_load_pairs, silent_write_num, silent_write_pairs,
//                           dead_write_num, dead_write_pairs);
//  show_srag_redundancy(srag_trace_map, threadid_max, srag_distribution);
//    calc_vr_redundancy_rate(index);
//    filter_dead_copy();
  show_hr_redundancy(hr_trace_map);
}

int main(int argc, char *argv[]) {

  init();
  auto result = options.parse(argc, argv);
  read_log_file(result["log"].as<string>());
  read_input_file(result["input"].as<string>());
  return 0;
}
