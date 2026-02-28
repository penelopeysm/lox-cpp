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
  // NOTE: The reason why std::string& comes first is because the key type of
  // the map is std::string. (I couldn't figure out where this is specified,
  // but looking at the implementation of std::unordered_map this seems to be
  // the case.)
  bool operator()(const std::string& a, const std::string& b) const {
    return a == b;
  }
  bool operator()(const std::string& a, std::string_view b) const {
    return a == b;
  }
  bool operator()(const std::string& a, ObjString* b) const {
    return a == b->value;
  }
};

template <typename Val> class StringMap {
public:
  StringMap() = default;
  std::unordered_map<std::string, Val, string_hash, string_eq> map;
};

} // namespace lox
