#ifndef REDSHOW_VALUE_FLOW_H
#define REDSHOW_VALUE_FLOW_H

#include <string>

std::string compute_memory_hash(uint64_t start, uint64_t len);

double compute_memory_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len);

#endif  // REDSHOW_VALUE_FLOW_H
