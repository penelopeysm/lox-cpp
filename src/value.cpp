#include "value.hpp"
#include "stringmap.hpp"
#include <iostream>

namespace {
struct LoxValuePrinter {
  std::ostream& os;
  void operator()(std::monostate) { os << "nil"; }
  void operator()(bool b) { os << (b ? "true" : "false"); }
  void operator()(double d) { os << d; }
  void operator()(lox::Obj* s) { os << s->to_repr(); }
};
} // namespace

namespace lox {

bool operator==(const Upvalue& a, const Upvalue& b) {
  return a.index == b.index && a.is_local == b.is_local;
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  std::visit(LoxValuePrinter{os}, value);
  return os;
}

Value ObjString::add(const Obj* other, StringMap& string_map) {
  if (other->type != ObjType::STRING) {
    throw std::runtime_error(
        "loxc: add: cannot concatenate non-string to string");
  }
  // no choice here, we have to concatenate the strings which means allocating
  auto other_str = static_cast<const ObjString*>(other);
  std::string new_str = value + other_str->value;
  return string_map.get_ptr(new_str);
}

Value ObjNativeFunction::call(uint8_t arg_count, const Value* args) {
  if (arg_count != arity) {
    throw std::runtime_error("expected " + std::to_string(arity) +
                             " arguments but got " + std::to_string(arg_count));
  }
  return function(arg_count, args);
}

bool is_truthy(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) {
    return false;
  } else if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  } else if (std::holds_alternative<double>(value)) {
    // all Lox numbers are truthy (even 0)
    return true;
  } else if (std::holds_alternative<Obj*>(value)) {
    // everything else is also truthy (even empty string)
    return true;
  } else {
    throw std::runtime_error("loxc: is_truthy: unknown value type");
  }
}

bool is_equal(const Value& a, const Value& b) {
  // Check if `a` and `b` have the same variant index (i.e. their type is the
  // same)
  if (a.index() != b.index()) {
    return false;
  }

  if (std::holds_alternative<std::monostate>(a)) {
    return true;
  } else if (std::holds_alternative<bool>(a)) {
    return std::get<bool>(a) == std::get<bool>(b);
  } else if (std::holds_alternative<double>(a)) {
    return std::get<double>(a) == std::get<double>(b);
  } else if (std::holds_alternative<Obj*>(a)) {
    // Because all strings are interned, pointer equality is sufficient
    return a == b;
  } else {
    throw std::runtime_error("unreachable in is_equal: unknown value type");
  }
}

Value add(const Value& a, const Value& b, StringMap& string_map) {
  if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
    return std::get<double>(a) + std::get<double>(b);
  } else if (std::holds_alternative<Obj*>(a) &&
             std::holds_alternative<Obj*>(b)) {
    auto aptr = std::get<Obj*>(a);
    auto bptr = std::get<Obj*>(b);
    return aptr->add(bptr, string_map);
  } else {
    throw std::runtime_error(
        "operands to `+` must be two numbers or two strings");
  }
}

} // namespace lox
