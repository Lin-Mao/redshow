#ifndef REDSHOW_COMMON_MAP_H
#define REDSHOW_COMMON_MAP_H

#include <map>
#include <mutex>

namespace redshow {

template <typename K, typename V>
class Map : public std::map<K, V> {
 public:
  Map() = default;

  // Not conflict with "contains" in C++20
  bool has(const K &k) const noexcept { return this->find(k) != this->end(); }
};

template <typename K, typename V>
class LockableMap : public Map<K, V> {
 public:
  void lock() const { _lock.lock(); }
  void unlock() const { _lock.unlock(); }

  LockableMap() = default;

  typename Map<K, V>::iterator prev(K &key) {
    auto iter = this->upper_bound(key);
    if (iter == this->begin()) {
      return this->end();
    } else {
      --iter;
      return iter;
    }
  }

  typename Map<K, V>::const_iterator prev(K &key) const {
    auto iter = this->upper_bound(key);
    if (iter == this->begin()) {
      return this->end();
    } else {
      --iter;
      return iter;
    }
  }

 private:
  mutable std::mutex _lock;
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_MAP_H