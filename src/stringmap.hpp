#pragma once

#include "value.hpp"
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace lox {

// https://www.cppstories.com/2021/heterogeneous-access-cpp20/
struct string_hash {
  using is_transparent = void;
  [[nodiscard]] size_t operator()(std::string_view txt) const {
    return std::hash<std::string_view>{}(txt);
  }
  [[nodiscard]] size_t operator()(const std::string& txt) const {
    return std::hash<std::string>{}(txt);
  }
};

class StringMap {
public:
  StringMap() = default;
  // Get a shared_ptr to the interned ObjString object. If the key doesn't
  // already exist this function will also create and store a new ObjString
  // in the map (but this is transparent to the caller).
  std::shared_ptr<ObjString> get_ptr(std::string_view key);

private:
  std::unordered_map<std::string, std::shared_ptr<ObjString>, string_hash, std::equal_to<>> map;
};

} // namespace lox
