#ifndef REDSHOW_ANALYSIS_DATA_FLOW_H
#define REDSHOW_ANALYSIS_DATA_FLOW_H

#include <mutex>
#include <string>

#include "analysis.h"
#include "binutils/cubin.h"
#include "common/graph.h"
#include "common/map.h"
#include "common/set.h"
#include "common/utils.h"
#include "operation/kernel.h"
#include "operation/memcpy.h"
#include "operation/memory.h"
#include "operation/memset.h"
#include "operation/operation.h"
#include "redshow.h"

namespace redshow {

class DataFlow final : public Analysis {
 public:
  DataFlow() : Analysis(REDSHOW_ANALYSIS_DATA_FLOW) { init(); }

  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mode_id,
                              GPUPatchType type);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id, const AccessKind &access_kind,
                           const Memory &memory, u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  virtual void flush_thread(u32 cpu_thread, const std::string &output_dir,
                            const LockableMap<u32, Cubin> &cubins,
                            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);

  ~DataFlow() {}

 private:
  typedef i32 Index;

  struct Node {
    Index ctx_id;
    OperationType type;

    Node(i32 ctx_id, OperationType type) : ctx_id(ctx_id), type(type) {}

    Node() : Node(0, OPERATION_TYPE_KERNEL) {}
  };

  enum EdgeType { DATA_FLOW_EDGE_ORDER, DATA_FLOW_EDGE_READ, DATA_FLOW_EDGE_SINK };

  std::string get_data_flow_edge_type(EdgeType type);

  struct EdgeIndex {
    Index from;
    Index to;
    i32 mem_ctx_id;
    EdgeType type;

    EdgeIndex() = default;

    EdgeIndex(Index &from, Index &to, i32 mem_ctx_id, EdgeType type)
        : from(from), to(to), mem_ctx_id(mem_ctx_id), type(type) {}

    bool operator<(const EdgeIndex &other) const {
      if (this->from == other.from) {
        if (this->to == other.to) {
          if (this->mem_ctx_id == other.mem_ctx_id) {
            return this->type < other.type;
          }
          return this->mem_ctx_id < other.mem_ctx_id;
        }
        return this->to < other.to;
      }
      return this->from < other.from;
    }
  };

  struct Edge {
    EdgeType type;
    u64 redundancy;
    u64 overwrite;  // write count
    u64 count;      // total byte count

    Edge() = default;

    Edge(EdgeType type, u64 redundancy, u64 overwrite)
        : type(type), redundancy(redundancy), overwrite(overwrite), count(0) {}

    Edge(EdgeType type) : Edge(type, 0, 0) {}
  };

  typedef Graph<Index, Node, EdgeIndex, Edge> DataFlowGraph;

  struct DataFlowTrace final : public Trace {
    Map<u64, Set<MemoryRange>> read_memory;
    Map<u64, Set<MemoryRange>> write_memory;

    DataFlowTrace() = default;

    virtual ~DataFlowTrace() {}
  };

 private:
  void init();

  void kernel_op_callback(std::shared_ptr<Kernel> op);

  void memory_op_callback(std::shared_ptr<Memory> op);

  void memset_op_callback(std::shared_ptr<Memset> op);

  void memcpy_op_callback(std::shared_ptr<Memcpy> op);

  void link_op_node(u64 op_id, i32 ctx_id, i32 mem_ctx_id);

  void link_ctx_node(i32 src_ctx_id, i32 dst_ctx_id, i32 mem_ctx_id, EdgeType type);

  void update_edge_metrics(i32 src_ctx_id, i32 dst_ctx_id, i32 mem_ctx_id, u64 redundancy,
                           u64 overwrite, u64 count, EdgeType type);

  void update_op_metrics(u64 op_id, i32 ctx_id, i32 mem_ctx_id, u64 redundancy, u64 overwrite,
                         u64 count, EdgeType type = DATA_FLOW_EDGE_ORDER);

  void update_op_node(u64 op_id, i32 ctx_id);

  void analyze_duplicate(Map<i32, Map<i32, bool>> &duplicate);

  void dump(const std::string &output_dir, const Map<i32, Map<i32, bool>> &duplicate);

  void merge_memory_range(Set<MemoryRange> &memory, const MemoryRange &memory_range);

 private:
  // Multi-threading is not allowable for data flow profiling
  std::shared_ptr<DataFlowTrace> _trace;

  DataFlowGraph _graph;
  Map<u64, i32> _op_node;
  Map<i32, Set<std::string>> _node_hash;
  Map<i32, u64> _node_count;
  Map<u64, std::shared_ptr<Memory>> _memories;
};

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_DATA_FLOW_H
