#pragma once
#include <variant>

namespace lox {
// NOTE: `using` is similar to `typedef`, but more powerful in C++
class Obj;
using Value = std::variant<std::monostate, bool, double, Obj*>;

constexpr bool is_double(const Value& value) {
  return std::holds_alternative<double>(value);
}
constexpr double as_double(const Value& value) {
  return std::get<double>(value);
}
constexpr bool is_bool(const Value& value) {
  return std::holds_alternative<bool>(value);
}
constexpr bool as_bool(const Value& value) { return std::get<bool>(value); }
constexpr bool is_obj(const Value& value) {
  return std::holds_alternative<Obj*>(value);
}
constexpr Obj* as_obj(const Value& value) { return std::get<Obj*>(value); }
constexpr bool is_nil(const Value& value) {
  return std::holds_alternative<std::monostate>(value);
}
constexpr std::monostate as_nil(const Value& value) {
  return std::get<std::monostate>(value);
}

} // namespace lox
