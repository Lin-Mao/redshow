#include "operation/operation.h"

namespace redshow {

const std::string get_operation_type(OperationType type) {
  static std::string operation_types[OPERATION_TYPE_COUNT] = {
      "OPERATION_TYPE_KERNEL", "OPERATION_TYPE_MEMORY", "OPERATION_TYPE_MEMCPY",
      "OPERATION_TYPE_MEMSET"};

  return operation_types[type];
}

Operation::~Operation() {}

}  // namespace redshow