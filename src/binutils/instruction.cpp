#include "binutils/instruction.h"

#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <queue>
#include <vector>

#include "binutils/symbol.h"
#include "common/utils.h"
#include "redshow.h"

#ifdef DEBUG_INSTRUCTION
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

namespace redshow {

void InstructionParser::default_access_kind(Instruction &inst) {
  if (inst.access_kind->vec_size == 0) {
    // Determine the vec size of data,
    if (inst.op.find(".128") != std::string::npos) {
      inst.access_kind->vec_size = 128;
    } else if (inst.op.find(".64") != std::string::npos) {
      inst.access_kind->vec_size = 64;
    } else if (inst.op.find(".32") != std::string::npos) {
      inst.access_kind->vec_size = 32;
    } else if (inst.op.find(".16") != std::string::npos) {
      inst.access_kind->vec_size = 16;
    } else if (inst.op.find(".8") != std::string::npos) {
      inst.access_kind->vec_size = 8;
    } else {
      inst.access_kind->vec_size = 32;
    }
  }

  // Default mode
  if (inst.access_kind->unit_size == 0) {
    // If unit size is not determined
    inst.access_kind->unit_size = inst.access_kind->vec_size;
  }

  if (inst.access_kind->data_type == REDSHOW_DATA_UNKNOWN) {
    // If type is not determined
    // TODO(Keren): it makes more sense to classify it as INT
    redshow_data_type_get(&(inst.access_kind->data_type));
  }
}

AccessKind InstructionParser::init_access_kind(Instruction &inst, InstructionGraph &inst_graph,
                                               std::set<unsigned int> &visited, bool load) {
  if (visited.find(inst.pc) != visited.end()) {
    // This node was visited before in the search for inst
    // It is still unknown as it appears again
    return AccessKind();
  }
  visited.insert(inst.pc);

  AccessKind access_kind;
  // Determine the vec size of data,
  if (inst.op.find(".128") != std::string::npos) {
    access_kind.vec_size = 128;
  } else if (inst.op.find(".64") != std::string::npos) {
    access_kind.vec_size = 64;
  } else if (inst.op.find(".32") != std::string::npos) {
    access_kind.vec_size = 32;
  } else if (inst.op.find(".16") != std::string::npos) {
    access_kind.vec_size = 16;
  } else if (inst.op.find(".8") != std::string::npos) {
    access_kind.vec_size = 8;
  } else {
    access_kind.vec_size = 32;
  }

  // Special handling for uniform register instructions
  if (inst.op.find("UNIFORM") != std::string::npos) {
    access_kind.data_type = REDSHOW_DATA_INT;
  }

  if ((load && inst_graph.outgoing_edge_size(inst.pc) == 0) ||
      (!load && inst_graph.incoming_edge_size(inst.pc) == 0)) {
    return access_kind;
  }

  auto &edges = load ? inst_graph.outgoing_edges(inst.pc) : inst_graph.incoming_edges(inst.pc);

  for (auto iter = edges.begin(); iter != edges.end(); ++iter) {
    auto pc = load ? iter->to : iter->from;
    auto &neighbor_inst = inst_graph.node(pc);
    AccessKind neighbor_access_kind;

    // Direct unit size detect
    if (access_kind.unit_size == 0) {
      if (neighbor_inst.op.find(".64") != std::string::npos) {
        access_kind.unit_size = MIN2(64, access_kind.vec_size);
      } else if (neighbor_inst.op.find(".32") != std::string::npos) {
        access_kind.unit_size = MIN2(32, access_kind.vec_size);
      } else if (neighbor_inst.op.find(".16") != std::string::npos) {
        access_kind.unit_size = MIN2(16, access_kind.vec_size);
      } else if (neighbor_inst.op.find(".8") != std::string::npos) {
        access_kind.unit_size = MIN2(8, access_kind.vec_size);
      } else if (neighbor_inst.op.find("._64_TO_32") != std::string::npos) {
        if (load) {
          access_kind.unit_size = MIN2(64, access_kind.vec_size);
        } else {
          access_kind.unit_size = MIN2(32, access_kind.vec_size);
        }
      } else if (neighbor_inst.op.find("._32_TO_64") != std::string::npos) {
        if (load) {
          access_kind.unit_size = MIN2(32, access_kind.vec_size);
        } else {
          access_kind.unit_size = MIN2(64, access_kind.vec_size);
        }
      }
    }

    if (neighbor_inst.op.find("MOVE") != std::string::npos) {
      // Transit node
      // INTEGER.IMAD.MOVE is handled here
      // Since a transit node is never a memory instruction, so do not cache its result
      neighbor_access_kind = init_access_kind(neighbor_inst, inst_graph, visited, load);
      if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
        access_kind.data_type = neighbor_access_kind.data_type;
      }
      if (access_kind.unit_size == 0) {
        access_kind.unit_size = neighbor_access_kind.unit_size;
      }
    } else if (neighbor_inst.op.find("MEMORY") != std::string::npos) {
      if (load) {
        // Decided by memory hierarchy
        if (neighbor_inst.op.find(".SHARED") != std::string::npos ||
            neighbor_inst.op.find(".LOCAL") != std::string::npos) {
          if (std::find(inst.dsts.begin(), inst.dsts.end(), neighbor_inst.srcs[0]) !=
              inst.dsts.end()) {
            if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
              access_kind.data_type = REDSHOW_DATA_INT;
            }
            if (access_kind.unit_size == 0) {
              access_kind.unit_size = 32;
            }
          }
        } else {
          if (std::find(inst.dsts.begin(), inst.dsts.end(), neighbor_inst.srcs[0]) !=
                  inst.dsts.end() ||
              std::find(inst.dsts.begin(), inst.dsts.end(), neighbor_inst.srcs[1]) !=
                  inst.dsts.end()) {
            if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
              access_kind.data_type = REDSHOW_DATA_INT;
            }
            if (access_kind.unit_size == 0) {
              access_kind.unit_size = 64;
            }
          }
        }
      } else {
        // Transit node and reverse search direction
        if (neighbor_inst.access_kind.get() == NULL) {
          // Cache result
          neighbor_inst.access_kind = std::make_shared<AccessKind>(
              init_access_kind(neighbor_inst, inst_graph, visited, !load));
          default_access_kind(neighbor_inst);
        }
        neighbor_access_kind = *neighbor_inst.access_kind;

        if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
          access_kind.data_type = neighbor_access_kind.data_type;
        }
        if (access_kind.unit_size == 0) {
          access_kind.unit_size = neighbor_access_kind.unit_size;
        }
      }
    } else if (neighbor_inst.op.find("INTEGER") != std::string::npos ||
               neighbor_inst.op.find("UNIFORM") != std::string::npos) {
      access_kind.data_type = REDSHOW_DATA_INT;
    } else if (neighbor_inst.op.find("FLOAT") != std::string::npos) {
      access_kind.data_type = REDSHOW_DATA_FLOAT;
    } else if (neighbor_inst.op.find("CONVERT") != std::string::npos) {
      if (neighbor_inst.op.find(".I2F") != std::string::npos) {
        if (load) {
          access_kind.data_type = REDSHOW_DATA_INT;
        } else {
          access_kind.data_type = REDSHOW_DATA_FLOAT;
        }
      } else if (neighbor_inst.op.find(".F2F") != std::string::npos) {
        access_kind.data_type = REDSHOW_DATA_FLOAT;
      } else if (neighbor_inst.op.find(".F2I") != std::string::npos) {
        if (load) {
          access_kind.data_type = REDSHOW_DATA_FLOAT;
        } else {
          access_kind.data_type = REDSHOW_DATA_INT;
        }
      } else if (neighbor_inst.op.find(".I2I") != std::string::npos) {
        access_kind.data_type = REDSHOW_DATA_INT;
      }
    } else {
      if (access_kind.data_type == REDSHOW_DATA_UNKNOWN) {
        access_kind.data_type = REDSHOW_DATA_INT;
      }
    }

    if (access_kind.data_type != REDSHOW_DATA_UNKNOWN && access_kind.unit_size != 0) {
      break;
    }
  }

  return access_kind;
}

bool InstructionParser::parse(const std::string &file_path, SymbolVector &symbols,
                              InstructionGraph &inst_graph) {
  boost::property_tree::ptree root;
  boost::property_tree::read_json(file_path, root);

#ifdef DEBUG_INSTRUCTION
  // inst_pc-><symbol_index, inst_pc_offset>
  std::map<int, std::pair<int, int>> pc_offsets;
#endif

  // Read instructions
  for (auto &ptree_function : root) {
    int function_index = ptree_function.second.get<int>("index", 0);
    int cubin_offset = ptree_function.second.get<int>("address", 0);
    // Ensure space
    symbols.resize(MAX2(symbols.size(), function_index + 1));
    symbols[function_index] = Symbol(function_index, cubin_offset);

    auto &ptree_blocks = ptree_function.second.get_child("blocks");
    for (auto &ptree_block : ptree_blocks) {
      auto &ptree_insts = ptree_block.second.get_child("insts");
      for (auto &ptree_inst : ptree_insts) {
        int pc = ptree_inst.second.get<int>("pc", 0) + cubin_offset;
        std::string op = ptree_inst.second.get<std::string>("op", "");
        int pred = ptree_inst.second.get<int>("pred", -1);

        std::vector<int> dsts;
        auto &ptree_dsts = ptree_inst.second.get_child("dsts");
        for (auto &ptree_dst : ptree_dsts) {
          int dst = boost::lexical_cast<int>(ptree_dst.second.data());
          dsts.push_back(dst);
        }

        std::vector<int> srcs;
        std::map<int, std::vector<int>> assign_pcs;
        auto &ptree_srcs = ptree_inst.second.get_child("srcs");
        for (auto &ptree_src : ptree_srcs) {
          int src = ptree_src.second.get<int>("id", 0);
          srcs.push_back(src);
          auto &ptree_assign_pcs = ptree_src.second.get_child("assign_pcs");
          for (auto &ptree_assign_pc : ptree_assign_pcs) {
            int assign_pc = boost::lexical_cast<int>(ptree_assign_pc.second.data()) + cubin_offset;
            assign_pcs[src].push_back(assign_pc);
          }
        }

        std::vector<int> udsts;
        auto &ptree_udsts = ptree_inst.second.get_child("udsts");
        for (auto &ptree_udst : ptree_udsts) {
          int udst = boost::lexical_cast<int>(ptree_udst.second.data());
          udsts.push_back(udst);
        }

        std::vector<int> usrcs;
        std::map<int, std::vector<int>> uassign_pcs;
        auto &ptree_usrcs = ptree_inst.second.get_child("usrcs");
        for (auto &ptree_usrc : ptree_usrcs) {
          int usrc = ptree_usrc.second.get<int>("id", 0);
          usrcs.push_back(usrc);
          auto &ptree_uassign_pcs = ptree_usrc.second.get_child("uassign_pcs");
          for (auto &ptree_uassign_pc : ptree_uassign_pcs) {
            int uassign_pc =
                boost::lexical_cast<int>(ptree_uassign_pc.second.data()) + cubin_offset;
            uassign_pcs[usrc].push_back(uassign_pc);
          }
        }

        Instruction inst(op, pc, pred, dsts, srcs, udsts, usrcs, assign_pcs, uassign_pcs);
        inst_graph.add_node(pc, inst);

#ifdef DEBUG_INSTRUCTION
        pc_offsets[pc] = std::make_pair(function_index, pc - cubin_offset);
#endif
      }
    }
  }

  // Build a instruction dependency graph
  for (auto iter = inst_graph.nodes_begin(); iter != inst_graph.nodes_end(); ++iter) {
    auto &inst = iter->second;

    size_t i = 0;
    if (inst.op.find("STORE") != std::string::npos) {
      // If store operation has more than one src, skip the first or two src
      if (inst.op.find(".SHARED") != std::string::npos ||
          inst.op.find(".LOCAL") != std::string::npos) {
        i = 1;
      } else {
        i = 2;
      }
    } else {
      i = 0;
    }

    for (; i < inst.srcs.size(); ++i) {
      int src = inst.srcs[i];
      for (auto src_pc : inst.assign_pcs[src]) {
        auto edge_index = InstructionDependencyIndex(src_pc, inst.pc);
        inst_graph.add_edge(std::move(edge_index), false);
      }
    }

    for (; i < inst.usrcs.size(); ++i) {
      int usrc = inst.usrcs[i];
      for (auto usrc_pc : inst.uassign_pcs[usrc]) {
        auto edge_index = InstructionDependencyIndex(usrc_pc, inst.pc);
        inst_graph.add_edge(std::move(edge_index), false);
      }
    }
  }

  // Analyze memory instruction's access kind
  for (auto iter = inst_graph.nodes_begin(); iter != inst_graph.nodes_end(); ++iter) {
    auto &inst = iter->second;

    if (inst.op.find("MEMORY") == std::string::npos) {
      continue;
    }

    if (inst.access_kind.get() != NULL) {
      // If access kind is cached
      continue;
    }

    // If access kind is not determined, allocate one
    inst.access_kind = std::make_shared<AccessKind>();
    std::set<unsigned int> visited;

    // Associate access type with instruction
    if (inst.op.find(".STORE") != std::string::npos) {
      *inst.access_kind = init_access_kind(inst, inst_graph, visited, false);
    } else if (inst.op.find(".LOAD") != std::string::npos) {
      *inst.access_kind = init_access_kind(inst, inst_graph, visited, true);
    }

    default_access_kind(inst);
  }

#ifdef DEBUG_INSTRUCTION
  // Analyze memory instruction's access kind
  for (auto iter = inst_graph.nodes_begin(); iter != inst_graph.nodes_end(); ++iter) {
    auto &inst = iter->second;
    if (inst.op.find("MEMORY") != std::string::npos) {
      std::cout << "Func Index: " << pc_offsets[inst.pc].first << ", PC: " << std::hex
                << pc_offsets[inst.pc].second << ", TYPE: " << inst.access_kind->to_string()
                << std::dec << std::endl;
    }
  }
#endif

  return true;
}

u64 AccessKind::value_to_basic_type(u64 a, int decimal_degree_f32, int decimal_degree_f64) {
  switch (data_type) {
    case REDSHOW_DATA_UNKNOWN:
      break;
    case REDSHOW_DATA_INT:
      switch (unit_size) {
        case 8:
          return a & 0xffu;
        case 16:
          return a & 0xffffu;
        case 32:
          return a & 0xffffffffu;
        case 64:
          return a;
      }
      break;
    case REDSHOW_DATA_FLOAT:
      switch (unit_size) {
        case 32:
          return value_to_float(a, decimal_degree_f32);
        case 64:
          return value_to_double(a, decimal_degree_f64);
      }
      break;
    default:
      break;
  }
  return a;
}

std::string AccessKind::value_to_string(u64 a, bool is_signed) {
  std::stringstream ss;
  if (data_type == REDSHOW_DATA_INT) {
    if (unit_size == 8) {
      if (is_signed) {
        i8 b;
        memcpy(&b, &a, sizeof(b));
        ss << (int)b;
      } else {
        u8 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      }
    } else if (unit_size == 16) {
      if (is_signed) {
        i16 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      } else {
        u16 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      }
    } else if (unit_size == 32) {
      if (is_signed) {
        i32 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      } else {
        u32 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      }
    } else if (unit_size == 64) {
      if (is_signed) {
        i64 b;
        memcpy(&b, &a, sizeof(b));
        ss << b;
      } else {
        ss << a;
      }
    }
  } else if (data_type == REDSHOW_DATA_FLOAT) {
    // At this time, it must be float
    if (unit_size == 32) {
      float b;
      memcpy(&b, &a, sizeof(b));
      ss << b;
    } else if (unit_size == 64) {
      double b;
      memcpy(&b, &a, sizeof(b));
      ss << b;
    }
  }

  return ss.str();
}

}  // namespace redshow
