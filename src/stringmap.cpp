#include "stringmap.hpp"
#include "gc.hpp"
#include "value.hpp"
#include <string_view>
#include <unordered_map>

namespace lox {

ObjString* StringMap::get_ptr(std::string_view key) {
  auto it = map.find(key);
  if (it == map.end()) {
#ifdef LOX_DEBUG
    std::cerr << "StringMap: Creating new ObjString for key: " << key
              << std::endl;
#endif
    // Not found; create a new one.
    ObjString* s = gc_new<ObjString>(key);
    auto new_pair = map.emplace(std::string(key), s);
    it = new_pair.first;
  } else {
#ifdef LOX_DEBUG
    std::cerr << "StringMap: Found existing ObjString for key: " << key
              << std::endl;
#endif
  }
  return it->second;
}

} // namespace lox
