#ifndef REDSHOW_ANALYSIS_VALUE_FLOW_H
#define REDSHOW_ANALYSIS_VALUE_FLOW_H

#include <mutex>
#include <string>

#include "analysis.h"
#include "common/graph.h"
#include "common/map.h"
#include "common/utils.h"
#include "operation/operation.h"
#include "redshow.h"

namespace redshow {

class ValueFlow final : public Analysis {
 public:
  ValueFlow() : Analysis(REDSHOW_ANALYSIS_VALUE_FLOW) {}

  // Derived functions
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           u64 memory_op_id, u64 pc, u64 value, u64 addr, u32 stride, u32 index,
                           bool read);

  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const Map<u32, Cubin> &cubins,
                            redshow_record_data_callback_func *record_data_callback);

  virtual void flush(const Map<u32, Cubin> &cubins, const std::string &output_dir,
                     const std::vector<OperationPtr> operations,
                     redshow_record_data_callback_func *record_data_callback);

 private:
  void update_op_node(u64 op_id, i32 ctx_id);
  
  void analyze_duplicate(const std::vector<OperationPtr> &ops,
                         Map<i32, Map<i32, bool>> &duplicate);

  void analyze_hot_api(const std::vector<OperationPtr> &value_flow_ops,
                       Map<i32, std::pair<double, int>> &hot_apis);

  void dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate,
            const Map<i32, std::pair<double, int>> &hot_apis);

 private:
  typedef i32 Index;

  struct Node {
    Index ctx_id;
    OperationType type;

    Node(i32 ctx_id, OperationType type) : ctx_id(ctx_id), type(type) {}

    Node() : Node(VALUE_FLOW_NODE_ALLOC, 0) {}
  };

  enum EdgeType { VALUE_FLOW_EDGE_ORDER, VALUE_FLOW_EDGE_READ };

  std::string get_value_flow_edge_type(EdgeType type);

  struct EdgeIndex {
    Index from;
    Index to;
    EdgeType type;

    EdgeIndex() = default;

    EdgeIndex(Index &from, Index &to, EdgeType type) : from(from), to(to), type(type) {}
  };

  struct Edge {
    EdgeType type;

    Edge() = default;

    Edge(EdgeType type) : type(type) {}
  };

  typedef Graph<NodeIndex, Node, EdgeIndex, Edge> ValueFlowGraph;

  struct Trace {
    Set<u64> read_memory_op_ids;
    Set<u64> write_memory_op_ids;
  };

 private:
  thread_local Trace *_trace;
  Map<u32, Map<i32, Trace>> _kernel_trace;

  ValueFlowGraph _graph;
  Map<u64, i32> _op_node;
  std::mutex _lock;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_VALUE_FLOW_H