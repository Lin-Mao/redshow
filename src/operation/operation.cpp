#include "operation/operation.h"

namespace redshow {

const std::string get_operation_type(OperationType type) {
  static std::string operation_types[OPERATION_TYPE_COUNT] = {"KERNEL", "MEMORY", "MEMCPY",
                                                              "MEMSET"};

  return operation_types[type];
}

Operation::~Operation() {}

}  // namespace redshow
