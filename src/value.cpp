#include "value.hpp"
#include "gc.hpp"
#include <ostream>

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

Value add(const Value& a, const Value& b, GC& gc) {
  if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
    return std::get<double>(a) + std::get<double>(b);
  } else if (std::holds_alternative<Obj*>(a) &&
             std::holds_alternative<Obj*>(b)) {
    auto aptr = std::get<Obj*>(a);
    auto bptr = std::get<Obj*>(b);
    if (aptr->type != ObjType::STRING || bptr->type != ObjType::STRING) {
      throw std::runtime_error(
          "operands to `+` must be two numbers or two strings");
    }
    auto str1 = static_cast<ObjString*>(aptr);
    auto str2 = static_cast<ObjString*>(bptr);
    // SUBTLE NOT-BUG: gc.get_string_ptr has to allocate, which means that it
    // might trigger GC, which in turn might clean up the old strings. But
    // that's fine, because this line will extract the data from the old strings
    // and concatenate them,
    std::string new_str = str1->value + str2->value;
    // so even if GC triggers here, we won't run into segfaults.
    return gc.get_string_ptr(new_str);
  } else {
    throw std::runtime_error(
        "operands to `+` must be two numbers or two strings");
  }
}

} // namespace lox
