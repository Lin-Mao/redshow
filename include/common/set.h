#ifndef REDSHOW_COMMON_SET_H
#define REDSHOW_COMMON_SET_H

#include <mutex>
#include <set>

namespace redshow {

template <typename K>
class Set : public std::set<K> {
 public:
  Set() = default;

  typename Set<K>::iterator prev(const K &key) {
    auto iter = this->upper_bound(key);
    if (iter == this->begin()) {
      return this->end();
    } else {
      --iter;
      return iter;
    }
  }

  typename Set<K>::const_iterator prev(const K &key) const {
    auto iter = this->upper_bound(key);
    if (iter == this->begin()) {
      return this->end();
    } else {
      --iter;
      return iter;
    }
  }

  // Not conflict with "contains" in C++20
  bool has(const K &k) const noexcept { return this->find(k) != this->end(); }
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_SET_H