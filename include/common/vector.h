#ifndef REDSHOW_COMMON_VECTOR_H
#define REDSHOW_COMMON_VECTOR_H

#include <vector>
#include <mutex>

namespace redshow {

template <typename V>
class Vector : public std::vector<V> {
 public:
  Vector() = default;

  // Not conflict with "contains" in C++20
  bool has(const V &v) const { return std::find(this->begin(), this->end(), v) != this->end(); }
};

template <typename V>
class LockableVector : public Vector<V> {
 public:
  void lock() { _lock.lock(); }
  void unlock() { _lock.unlock(); }

  LockableVector() = default;

 private:
  std::mutex _lock;
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_MAP_H