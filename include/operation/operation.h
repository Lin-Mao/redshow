#ifndef REDSHOW_ANALYSIS_OPERATION_H
#define REDSHOW_ANALYSIS_OPERATION_H

#include <memory>
#include <string>

#include "common/utils.h"

namespace redshow {

enum OperationType {
  OPERATION_TYPE_KERNEL = 0,
  OPERATION_TYPE_MEMORY = 1,
  OPERATION_TYPE_MEMCPY = 2,
  OPERATION_TYPE_MEMSET = 3,
  OPERATION_TYPE_COUNT = 4
};

const std::string get_operation_type(OperationType type);

struct Operation {
  u64 op_id;
  i32 ctx_id;
  OperationType type;

  Operation() = default;

  Operation(u64 op_id, i32 ctx_id, OperationType type) : op_id(op_id), ctx_id(ctx_id), type(type) {}

  virtual ~Operation() = 0;
};

typedef std::shared_ptr<Operation> OperationPtr;

}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_OPERATION_H
