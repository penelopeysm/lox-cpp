#pragma once
#ifdef NAN_BOXING
#include <bit>
#include <cstdint>
#else
#include <variant>
#endif

namespace lox {

class Obj; // Forward decl

#ifdef NAN_BOXING

struct Value {
  uint64_t data;
};

constexpr uint64_t QNAN = 0x7ffc000000000000;
constexpr uint64_t NIL = QNAN | 1;
constexpr uint64_t FALSE = QNAN | 2;
constexpr uint64_t TRUE = QNAN | 3;

constexpr bool is_double(const Value& value) {
  return (value.data & QNAN) != QNAN;
}
constexpr double as_double(const Value& value) {
  return std::bit_cast<double>(value.data);
}
constexpr Value from_double(const double& d) {
  return Value{std::bit_cast<Value>(d)};
}

constexpr bool is_bool(const Value& value) { return (value.data | 1) == TRUE; }
constexpr bool as_bool(const Value& value) { return (value.data & 1) == 1; }
constexpr Value from_bool(const bool& b) {
  return b ? Value{TRUE} : Value{FALSE};
}

constexpr bool is_nil(const Value& value) { return value.data == NIL; }
constexpr Value nil_val() { return Value{NIL}; }

constexpr uint64_t OBJ_MASK = 0xFFFF000000000000;
constexpr uint64_t OBJ_VAL_MASK = 0x0000FFFFFFFFFFFF;

constexpr bool is_obj(const Value& value) { return (value.data & OBJ_MASK) == OBJ_MASK; }
constexpr Obj* as_obj(const Value& value) {
  return std::bit_cast<Obj*>(value.data & OBJ_VAL_MASK);
}
constexpr Value from_obj(Obj* o) {
  return Value{std::bit_cast<uint64_t>(o) | OBJ_MASK};
}

#else
// NOTE: `using` is similar to `typedef`, but more powerful in C++
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
constexpr Value nil_val() { return std::monostate{}; }

constexpr Value from_bool(bool b) { return b; }
constexpr Value from_double(double d) { return d; }
constexpr Value from_obj(Obj* o) { return o; }
constexpr Value from_nil(std::monostate) { return std::monostate{}; }
#endif

} // namespace lox
