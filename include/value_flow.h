#ifndef REDSHOW_VALUE_FLOW_H
#define REDSHOW_VALUE_FLOW_H

#include <string>

namespace redshow {

/**
 * @brief calculate a hash for the memory region
 *
 * @param start
 * @param len
 * @return std::string
 */
std::string compute_memory_hash(uint64_t start, uint64_t len);

/**
 * @brief calculate byte redundancy rate between two memory regions
 *
 * @param dst_start
 * @param src_start
 * @param len
 * @return double
 */
double compute_memcpy_redundancy(uint64_t dst_start, uint64_t src_start, uint64_t len);

/**
 * @brief calculate byte redundancy rate of memset operations
 *
 * @param start
 * @param value
 * @param len
 * @return double
 */
double compute_memset_redundancy(uint64_t start, uint32_t value, uint64_t len);

}  // namespace redshow

#endif  // REDSHOW_VALUE_FLOW_H
