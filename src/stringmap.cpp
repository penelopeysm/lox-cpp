#include "stringmap.hpp"
#include "value.hpp"

namespace lox {

size_t string_hash::operator()(ObjString* obj) const {
  return std::hash<std::string>{}(obj->value);
}

std::string_view string_eq::to_view(ObjString* obj) { return obj->value; }

} // namespace lox
