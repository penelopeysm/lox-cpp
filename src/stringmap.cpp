#include "stringmap.hpp"
#include "value.hpp"
#include <memory>
#include <string_view>
#include <unordered_map>

namespace lox {

std::shared_ptr<ObjString> StringMap::get_ptr(std::string_view key) {
  auto it = map.find(key);
  if (it == map.end()) {
#ifdef LOX_DEBUG
    std::cout << "StringMap: Creating new ObjString for key: " << key
              << std::endl;
#endif
    // Not found; create a new one.
    std::shared_ptr<ObjString> ptr = std::make_shared<ObjString>(key);
    auto new_pair = map.emplace(std::string(key), ptr);
    it = new_pair.first;
  } else {
#ifdef LOX_DEBUG
    std::cout << "StringMap: Found existing ObjString for key: " << key
              << std::endl;
#endif
  }
  return it->second;
}

} // namespace lox
