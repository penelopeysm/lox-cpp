#include "stringmap.hpp"
#include "value.hpp"
#include <memory>
#include <string_view>
#include <unordered_map>

namespace lox {

std::shared_ptr<ObjString> StringMap::get_ptr(std::string_view key) {
  auto it = map.find(key);
  if (it == map.end()) {
    // Not found; create a new one.
    std::shared_ptr<ObjString> ptr = std::make_shared<ObjString>(key);
    auto new_pair = map.emplace(std::string(key), ptr);
    it = new_pair.first;
  }
  return it->second;
}

} // namespace lox
