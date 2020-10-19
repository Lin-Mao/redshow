#ifndef REDSHOW_COMMON_SET_H
#define REDSHOW_COMMON_SET_H

#include <set>
#include <mutex>

namespace redshow {

template <typename K>
class Set : public std::set<K> {
 public:
  Set() = default;

  // Not conflict with "contains" in C++20
  bool has(const K &k) const noexcept { return this->find(k) != this->end(); }
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_SET_H