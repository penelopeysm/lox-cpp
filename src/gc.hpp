#pragma once
#include <utility>

template <typename T, typename... Args> T* gc_new(Args&&... args) {
  T* obj = new T(std::forward<Args>(args)...);
  // TODO: add obj to GC tracking system(?)
  return obj;
}
