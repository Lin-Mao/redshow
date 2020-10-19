#ifndef REDSHOW_COMMON_VECTOR_H
#define REDSHOW_COMMON_VECTOR_H

#include <vector>
#include <mutex>

namespace redshow {

template <typename V>
class Vector : public std::vector<V> {
 public:
  Vector() = default;

  Vector(size_t size) : std::vector<V>(size) {}

  // Not conflict with "contains" in C++20
  bool has(const V &v) const noexcept {
    return std::find(this->begin(), this->end(), v) != this->end();
  }
};

template <typename V>
class LockableVector : public Vector<V> {
 public:
  void lock() const { _lock.lock(); }
  void unlock() const { _lock.unlock(); }

  LockableVector() = default;

 private:
  mutable std::mutex _lock;
};

}  // namespace redshow

#endif  // REDSHOW_COMMON_MAP_H