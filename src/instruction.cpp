#include "instruction.h"
#include "redshow.h"

#include <algorithm>
#include <iostream>
#include <fstream>
#include <vector>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>

#include <cstdlib>

#ifdef DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

#define MIN2(x, y) (x > y ? y : x)

bool parse_instructions(const std::string &file_path,
  std::vector<Symbol> &symbols, InstructionGraph &inst_graph) {
  boost::property_tree::ptree root;
  boost::property_tree::read_json(file_path, root);

  // Read instructions
  for (auto &ptree_function : root) {
    int function_index = ptree_function.second.get<int>("index", 0); 
    int cubin_offset = ptree_function.second.get<int>("address", 0); 
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
        std::map<int, std::vector<int> > assign_pcs;
        auto &ptree_srcs = ptree_inst.second.get_child("srcs");
        for (auto &ptree_src : ptree_srcs) {
          int src = ptree_src.second.get<int>("id", 0); 
          srcs.push_back(src);
          auto &ptree_assign_pcs = ptree_src.second.get_child("assign_pcs");
          for (auto &ptree_assign_pc : ptree_assign_pcs) {
            int assign_pc = boost::lexical_cast<int>(ptree_assign_pc.second.data())
              + cubin_offset;
            assign_pcs[src].push_back(assign_pc);
          }   
        }  

        Instruction inst(op, pc, pred, dsts, srcs, assign_pcs);
        inst_graph.add_node(pc, inst);
      }
    }
  }

  // Build a instruction dependency graph
  for (auto iter = inst_graph.nodes_begin(); iter != inst_graph.nodes_end(); ++iter) {
    auto &inst = iter->second;

    size_t i = 0;
    if (inst.op.find("STORE") != std::string::npos) {
      // If store operation has more than one src, skip the first or two src
      if (inst.op.find(".SHARED") != std::string::npos || inst.op.find(".LOCAL") != std::string::npos) {
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
        inst_graph.add_edge(src_pc, inst.pc);

#ifdef DEBUG
        std::cout << "Add edge: 0x" << std::hex << src_pc << ", 0x" <<
          inst.pc << std::dec << std::endl;
#endif
      }
    }
  }

  return true;
}


static AccessType init_access_type(Instruction &inst, InstructionGraph &inst_graph,
  const std::set<unsigned int> &neighbors) {
  // Determine the vec size of data,
  if (inst.op.find(".128") != std::string::npos) {
    inst.access_type->vec_size = 128;
  } else if (inst.op.find(".64") != std::string::npos) {
    inst.access_type->vec_size = 64;
  } else if (inst.op.find(".32") != std::string::npos) { 
    inst.access_type->vec_size = 32;
  } else if (inst.op.find(".16") != std::string::npos) {
    inst.access_type->vec_size = 16;
  } else if (inst.op.find(".8") != std::string::npos) {
    inst.access_type->vec_size = 8;
  } else {
    inst.access_type->vec_size = 32;
  }

  // Determine the unit size of data,
  // STORE: check the usage of its src regs
  // LOAD: check the usage of its dst regs
  for (auto iter = neighbors.begin(); iter != neighbors.end(); ++iter) {
    auto &dst_inst = inst_graph.node(*iter);

    if (inst.access_type->unit_size == 0) {
      if (dst_inst.op.find(".64") != std::string::npos) {
        inst.access_type->unit_size = MIN2(64, inst.access_type->vec_size);
      } else if (dst_inst.op.find(".32") != std::string::npos) {
        inst.access_type->unit_size = MIN2(32, inst.access_type->vec_size);
      } else if (dst_inst.op.find(".16") != std::string::npos) {
        inst.access_type->unit_size = MIN2(16, inst.access_type->vec_size);
      } else if (dst_inst.op.find(".8") != std::string::npos) {
        inst.access_type->unit_size = MIN2(8, inst.access_type->vec_size);
      }
    }

    if (inst.access_type->type == AccessType::UNKNOWN) {
      if (dst_inst.op.find("FLOAT") != std::string::npos) {
        inst.access_type->type = AccessType::FLOAT;
      } else if (dst_inst.op.find("INTEGER") != std::string::npos) {
        inst.access_type->type = AccessType::INTEGER;
      } 
    }
  }

  if (inst.access_type->unit_size == 0) {
    // If unit size is not determined
    inst.access_type->unit_size = MIN2(32, inst.access_type->vec_size);
  }

  if (inst.access_type->type == AccessType::UNKNOWN) {
    // If type is not determined
    inst.access_type->type = AccessType::INTEGER;
  }
}


AccessType load_data_type(unsigned int pc, InstructionGraph &inst_graph) {
  auto &inst = inst_graph.node(pc);

  if (inst.access_type.get() != NULL) {
    // If access type is cached
    return *(inst.access_type);
  }

  // If access type is undetermined, allocate one
  inst.access_type = std::make_shared<AccessType>();

  // If we cannot determine the access type
  if (inst.op.find(".LOAD") != std::string::npos &&
    inst_graph.outgoing_nodes_size(pc) != 0) {
    auto &outgoing_nodes = inst_graph.outgoing_nodes(pc);

    // Associate access type with instruction
    init_access_type(inst, inst_graph, outgoing_nodes);
  }

  return *(inst.access_type);
}


AccessType store_data_type(unsigned int pc, InstructionGraph &inst_graph) {
  auto &inst = inst_graph.node(pc);

  if (inst.access_type.get() != NULL) {
    // If access type is cached
    return *(inst.access_type);
  }

  // If access type is undetermined, allocate one
  inst.access_type = std::make_shared<AccessType>();

  // If we cannot determine the access type
  if (inst.op.find(".STORE") != std::string::npos &&
    inst_graph.incoming_nodes_size(pc) != 0) {
    auto &incoming_nodes = inst_graph.incoming_nodes(pc);

    // Associate access type with instruction
    init_access_type(inst, inst_graph, incoming_nodes);
  }

  return *(inst.access_type);
}
