#ifndef REDSHOW_ANALYSIS_VALUE_FLOW_H
#define REDSHOW_ANALYSIS_VALUE_FLOW_H

#include <mutex>
#include <string>

#include "analysis.h"
#include "binutils/cubin.h"
#include "common/graph.h"
#include "common/map.h"
#include "common/set.h"
#include "common/utils.h"
#include "operation/operation.h"
#include "redshow.h"

namespace redshow {

class ValueFlow final : public Analysis {
 public:
  ValueFlow() : Analysis(REDSHOW_ANALYSIS_VALUE_FLOW) {}

  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mode_id);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           u64 memory_op_id, u64 pc, u64 value, u64 addr, u32 stride, u32 index,
                           bool read);

  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     const Vector<OperationPtr> &operations,
                     redshow_record_data_callback_func record_data_callback);

  ~ValueFlow() {}

 private:
  typedef i32 Index;

  struct Node {
    Index ctx_id;
    OperationType type;

    Node(i32 ctx_id, OperationType type) : ctx_id(ctx_id), type(type) {}

    Node() : Node(0, OPERATION_TYPE_KERNEL) {}
  };

  enum EdgeType { VALUE_FLOW_EDGE_ORDER, VALUE_FLOW_EDGE_READ };

  std::string get_value_flow_edge_type(EdgeType type);

  struct EdgeIndex {
    Index from;
    Index to;
    EdgeType type;

    EdgeIndex() = default;

    EdgeIndex(Index &from, Index &to, EdgeType type) : from(from), to(to), type(type) {}

    bool operator<(const EdgeIndex &other) const {
      if (this->from == other.from) {
        if (this->to == other.to) {
          return this->type < other.type;
        }
        return this->to < other.to;
      }
      return this->from < other.from;
    }
  };

  struct Edge {
    EdgeType type;

    Edge() = default;

    Edge(EdgeType type) : type(type) {}
  };

  typedef Graph<Index, Node, EdgeIndex, Edge> ValueFlowGraph;

  struct ValueFlowTrace final : public Trace {
    Set<u64> read_memory_op_ids;
    Set<u64> write_memory_op_ids;

    ValueFlowTrace() = default;

    virtual ~ValueFlowTrace() {}
  };

 private:
  void link_ctx_node(i32 src_ctx_id, i32 dst_ctx_id, EdgeType type);

  void update_op_node(u64 op_id, i32 ctx_id);

  void analyze_duplicate(const Vector<OperationPtr> &ops, Map<i32, Map<i32, bool>> &duplicate);

  void analyze_hot_api(const Vector<OperationPtr> &value_flow_ops,
                       Map<i32, std::pair<double, int>> &hot_apis);

  void analyze_overwrite(const Vector<OperationPtr> &value_flow_ops,
                         Map<i32, std::pair<double, int>> &overwrite_rate);

  void dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate,
            const Map<i32, std::pair<double, int>> &hot_apis,
            const Map<i32, std::pair<double, int>> &overwrite_rate);

 private:
  static inline thread_local std::shared_ptr<ValueFlowTrace> _trace;

  ValueFlowGraph _graph;
  Map<u64, i32> _op_node;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_VALUE_FLOW_H
