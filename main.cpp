#include "common_lib.h"
#include "cxxopts.hpp"

using cxxopts::Options;

// init all arrays
void initall();

void freeall();

// The values' types.
enum BasicType {
  F32, F64, S64, U64, S32, U32, S8, U8
};

inline void output_corresponding_type_value(u64 a, BasicType atype, streambuf *buf) {
  ostream out(buf);
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
      out << b3;
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
      u32 b5;
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

inline void output_corresponding_type_value(u64 a, AccessType atype, std::streambuf *buf, bool is_signed) {
  ostream out(buf);
  if (atype.type == AccessType::INTEGER) {
    if (atype.unit_size == 8) {
      if (is_signed) {
        char b6;
        memcpy(&b6, &a, sizeof(b6));
        out << b6;
      } else {
        _u8 b7;
        memcpy(&b7, &a, sizeof(b7));
        out << b7;
      }
    } else if (atype.unit_size == 16) {
      if (is_signed) {
        short int b8;
        memcpy(&b8, &a, sizeof(b8));
        out << b8;
      } else {
        unsigned short int b9;
        memcpy(&b9, &a, sizeof(b9));
        out << b9;
      }
    } else if (atype.unit_size == 32) {
      if (is_signed) {
        int b4;
        memcpy(&b4, &a, sizeof(b4));
        out << b4;
      } else {
        u32 b5;
        memcpy(&b5, &a, sizeof(b5));
        out << b5;
      }
    } else if (atype.unit_size == 64) {
      if (is_signed) {
        long long b3;
        memcpy(&b3, &a, sizeof(b3));
        out << b3;
      } else {
        out << a;
      }
    }
//    At this time, it must be float
  } else {
    if (atype.unit_size == 32) {
      float b1;
      memcpy(&b1, &a, sizeof(b1));
      out << b1;
    } else if (atype.unit_size == 64) {
      double b2;
      memcpy(&b2, &a, sizeof(b2));
      out << b2;
    }
  }

}

u64 store2uchar(u64 a) {
  return a & 0xffu;
}


u64 store2uint(u64 a) {
  u64 c = 0;
  c = ((a & 0xffu) << 24u)
      | ((a & 0xff00u) << 8u) | ((a & 0xff0000u) >> 8u) | ((a & 0xff000000u) >> 24u);
  return c;
}

// convert an hex value to float format. Use decimal_degree_f32 to control precision. We still store
u64 store2float(u64 a, int decimal_degree_f32) {
  u32 c = store2uint(a);
  u32 mask = 0xffffffff;
  for (int i = 0; i < 23 - decimal_degree_f32; ++i) {
    mask <<= 1u;
  }
  c &= mask;
  u64 b = 0;
  memcpy(&b, &c, sizeof(c));
  return b;
}

// Mainly change the expression from big endian to little endian.
u64 store2u64(u64 a) {
  u64 c = 0;
  c = ((a & 0xffu) << 56u) | ((a & 0xff00u) << 40u) | ((a & 0xff0000u) << 24u) | ((a & 0xff000000u) << 8u) |
      ((a & 0xff00000000u) >> 8u) | ((a & 0xff0000000000u) >> 24u) | ((a & 0xff000000000000u) >> 40u) |
      ((a & 0xff00000000000000u) >> 56u);
  return c;
}


u64 store2double(u64 a, int decimal_degree_f64) {
  u64 c = store2u64(a);
  u64 mask = 0xffffffffffffffff;
  for (int i = 0; i < 52 - decimal_degree_f64; ++i) {
    mask <<= 1u;
  }
  c = c & mask;
  return c;
}


bool equal_2_tuples(std::tuple<long long, long long, u64> a, std::tuple<long long, long long> b) {
  return std::get<0>(a) == std::get<0>(b) && std::get<1>(a) == std::get<1>(b);
}


ThreadId transform_tid(std::string s_bid, std::string s_tid) {
// This function will transform the raw string of bid and tid to struct ThreadId
// @arg s_bid: (2,0,0): (bx,by,bz)
//  regex tid_re(R"((\d+),(\d+),(\d+))");
// merge new branch, so the threadid is changed to flat_thread_id
  std::regex tid_re(R"((\d+))");
  ThreadId tid;
  std::smatch sm;
  regex_match(s_bid, sm, tid_re);
  if (sm.empty()) {
    return tid;
  }
  tid.flat_block_id = stoi(sm[1], 0, 10);
  regex_match(s_tid, sm, tid_re);
  if (sm.empty()) {
    return tid;
  }
  tid.flat_thread_id = stoi(sm[1], 0, 10);
  return tid;
}


ThreadId get_max_threadId(ThreadId a, ThreadId threadid_max) {
  threadid_max.flat_block_id = std::max(threadid_max.flat_block_id, a.flat_block_id);
  threadid_max.flat_thread_id = std::max(threadid_max.flat_thread_id, a.flat_thread_id);
  return threadid_max;
}


void init();

void read_input_file(const string &input_file, string target_name);

ostream &operator<<(ostream &out, const ThreadId &A) {
  out << "(" << A.flat_block_id << ")(" << A.flat_thread_id << ")";
  return out;
}

extern map<ThreadId, list<u64 >> tra_list;
extern map<u64, vector<int >> tra_trace_map;
extern map<int, map<int, u64 >> tra_rd_dist;
extern map<ThreadId, map<u64, tuple<u64, u64, u64>>> trv_map_read;
extern map<ThreadId, map<u64, tuple<u64, u64, u64>>> trv_map_write;
extern vector<tuple<u64, u64, u64, u64, BasicType>> silent_load_pairs;
extern vector<tuple<u64, u64, u64, u64, BasicType>> silent_write_pairs;
extern vector<tuple<u64, u64, u64, u64, BasicType>> dead_write_pairs;
extern long long silent_load_num, dead_write_num, silent_write_num;
extern map<u64, map<ThreadId, vector<tuple<u64, u64>>>> srag_trace_map;
extern map<u64, map<ThreadId, set<u64>>> srag_trace_map_test;
extern map<u64, map<u64, map<ThreadId, u64 >>> srv_trace_map;
extern u64 srag_distribution[WARP_SIZE];
extern map<ThreadId, map<int, map<int, long long>>> srv_bs_trace_map;
extern map<tuple<u64, AccessType>, map<u64, u64 >> hr_trace_map;
extern set<u64> pcs;
extern map<int, set<u64 >> dc_trace_map;


Options options("CUDA_RedShow", "A test suit for hpctoolkit santizer");
// every array's memory start addr and size
vector<tuple<u64, int>> vars_mem_block;
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
  u64 addr;
  int var_size;
  int i = 0;
  while (regex_search(strdata, sm, log_read_re)) {
    addr = stoull(sm[1], 0, 16);
    var_size = stoi(sm[2], 0, 10);
    vars_mem_block.emplace_back(pair<u64, int>(addr, var_size));
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
  u64 index = 0;
  while (getline(fin, line)) {
    smatch sm;
    regex_match(line, sm, line_read_re);
    if (sm.empty()) {
      cout << "This line can't match the regex:\t" << line << endl;
      continue;
    }
    u64 pc, addr, value_hex;
    int access_type;
    pc = stoull(sm[1], 0, 16);
    addr = stoull(sm[4], 0, 16);
    ThreadId tid = transform_tid(sm[2], sm[3]);
    threadid_max = get_max_threadId(tid, threadid_max);
    auto belongs = get_cur_addr_belong_index(addr, vars_mem_block);
    int belong = get<0>(belongs);
    u64 offset = get<1>(belongs);
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
          u64 value = INT64_MAX;
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

          //get_hr_trace_map(value, (u64) belong, array_type[belong], hr_trace_map);
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
  //show_hr_redundancy(hr_trace_map);
}

int main(int argc, char *argv[]) {

  init();
  auto result = options.parse(argc, argv);
  read_log_file(result["log"].as<string>());
  read_input_file(result["input"].as<string>());
  return 0;
}
