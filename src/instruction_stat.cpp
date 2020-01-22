#include "instruction_stat.h"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/lexical_cast.hpp>


bool read_instruction_stats(const std::string &file_path, std::vector<InstructionStat> &inst_stats) {
  boost::property_tree::ptree root;

  boost::property_tree::read_json(file_path, root);

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
        auto &ptree_srcs = ptree_inst.second.get_child("srcs");
        for (auto &ptree_src : ptree_srcs) {
          int src = ptree_src.second.get<int>("id", 0);
          srcs.push_back(src);
        }

        inst_stats.emplace_back(InstructionStat(op, pc, pred, dsts, srcs));
      }
    }
  }
}

