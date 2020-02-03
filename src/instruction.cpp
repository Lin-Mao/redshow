#include "instruction.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>


bool parse_instruction_graph(const std::string &file_path, InstructionGraph &inst_graph) {
  boost::property_tree::ptree root;

  boost::property_tree::read_json(file_path, root);

  // Read instructions
  for (auto &ptree_function : root) {
    auto &ptree_blocks = ptree_function.second.get_child("blocks");
    for (auto &ptree_block : ptree_blocks) {
      auto &ptree_insts = ptree_block.second.get_child("insts");
      for (auto &ptree_inst : ptree_insts) {
        int pc = ptree_inst.second.get<int>("pc", 0);
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
            int assign_pc = ptree_assign_pc.second.get<int>("pc");
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

    for (auto dst_pc : inst.dsts) {
      inst_graph.add_edge(inst.pc, dst_pc);
    }

    for (auto src_pc : inst.srcs) {
      inst_graph.add_edge(src_pc, inst.pc);
    }
  }

  return true;
}


AccessType load_data_type(unsigned int pc, InstructionGraph &inst_graph) {
  AccessType access_type;

  const std::set<unsigned int> &outgoing_nodes = inst_graph.outgoing_nodes(pc);

  for (auto iter = outgoing_nodes.begin(); iter != outgoing_nodes.end(); ++iter) {
    auto &inst = inst_graph.node(*iter);
    if (inst.op.find("FLOAT") != std::string::npos) {
      access_type.type = AccessType::FLOAT;
    } else if (inst.op.find("INTEGER")) {
      access_type.type = AccessType::INTEGER;
    } 
    // TODO(Keren): complete mufu
    //else if (inst.op.find("MUFU")) {
    //}
    else {
      // default integer
      access_type.type = AccessType::INTEGER;
    }

    if (inst.op.find("64") != std::string::npos) {
      access_type.vec_size = 64;
    } else if (inst.op.find("32") != std::string::npos) {
      access_type.vec_size = 32;
    } else if (inst.op.find("16") != std::string::npos) {
      access_type.vec_size = 16;
    } else {
      // default 32
      access_type.vec_size = 32;
    }
  }

  return access_type;
}


AccessType store_data_type(unsigned int pc, InstructionGraph &inst_graph) {
  AccessType access_type;

  const std::set<unsigned int> &incoming_nodes = inst_graph.incoming_nodes(pc);

  for (auto iter = incoming_nodes.begin(); iter != incoming_nodes.end(); ++iter) {
    auto &inst = inst_graph.node(*iter);
    if (inst.op.find("FLOAT") != std::string::npos) {
      access_type.type = AccessType::FLOAT;
    } else if (inst.op.find("INTEGER")) {
      access_type.type = AccessType::INTEGER;
    } 
    // TODO(Keren): complete mufu
    //else if (inst.op.find("MUFU")) {
    //}
    else {
      // default integer
      access_type.type = AccessType::INTEGER;
    }

    if (inst.op.find("64") != std::string::npos) {
      access_type.vec_size = 64;
    } else if (inst.op.find("32") != std::string::npos) {
      access_type.vec_size = 32;
    } else if (inst.op.find("16") != std::string::npos) {
      access_type.vec_size = 16;
    } else {
      // default 32
      access_type.vec_size = 32;
    }
  }

  return access_type;
}
