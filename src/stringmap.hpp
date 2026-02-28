#pragma once
#include "value.hpp"
#include <string>
#include <unordered_map>

namespace lox {

// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
struct string_hash {
  // NOTE: `using` defines a type inside the struct. The value of the type
  // doesn't matter; it just matters that it's defined. The presence of this
  // type is what allows the unordered_map to use heterogeneous lookup (instead
  // of converting any lookup key to the key type, std::string, which can cause
  // allocations).
  using is_transparent = void;
  size_t operator()(std::string_view txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  size_t operator()(const std::string& txt) const {
    return std::hash<std::string>{}(txt);
  }
  size_t operator()(ObjString* obj) const {
    return std::hash<std::string>{}(obj->value);
  }
};

struct string_eq {
  using is_transparent = void;

  static std::string_view to_view(const std::string& s) { return s; }
  static std::string_view to_view(std::string_view s) { return s; }
  static std::string_view to_view(ObjString* obj) { return obj->value; }

  template <typename T, typename U>
  bool operator()(const T& t, const U& u) const {
    return to_view(t) == to_view(u);
  }
};

template <typename Val> class StringMap {
public:
  StringMap() = default;
  std::unordered_map<std::string, Val, string_hash, string_eq> map;
};

} // namespace lox
