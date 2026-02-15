#include "gc.hpp"
#include "value.hpp"
#include <string_view>
#include <unordered_map>

namespace lox {

ObjString* GC::get_string_ptr(std::string_view key) {
  auto it = interned_strings.map.find(key);
  if (it == interned_strings.map.end()) {
    // Not found; create a new one.
    ObjString* s = alloc<ObjString>(key);
    auto new_pair = interned_strings.map.emplace(std::string(key), s);
    it = new_pair.first;
  }
  return it->second;
}

} // namespace lox
