#include "value.hpp"
#include "stringmap.hpp"
#include <iostream>

namespace {
struct LoxValuePrinter {
  std::ostream& os;
  void operator()(std::monostate) { os << "nil"; }
  void operator()(bool b) { os << (b ? "true" : "false"); }
  void operator()(double d) { os << d; }
  void operator()(std::shared_ptr<lox::Obj> s) { os << s->to_repr(); }
};
} // namespace

namespace lox {

std::ostream& operator<<(std::ostream& os, const Value& value) {
  std::visit(LoxValuePrinter{os}, value);
  return os;
}

Value ObjString::add(const std::shared_ptr<Obj>& other, StringMap& string_map) {
  auto other_str = std::dynamic_pointer_cast<ObjString>(other);
  if (other_str == nullptr) {
    throw std::runtime_error(
        "loxc: add: cannot concatenate non-string to string");
  }
  // no choice here, we have to concatenate the strings which means allocating
  std::string new_str = value + other_str->value;
  return string_map.get_ptr(new_str);
}

bool is_truthy(const Value& value) {
  if (std::holds_alternative<std::monostate>(value)) {
    return false;
  } else if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value);
  } else if (std::holds_alternative<double>(value)) {
    // all Lox numbers are truthy (even 0)
    return true;
  } else if (std::holds_alternative<std::shared_ptr<Obj>>(value)) {
    // everything else is also truthy
    return true;
  } else {
    throw std::runtime_error("loxc: is_truthy: unknown value type");
  }
}

bool is_equal(const Value& a, const Value& b) {
  if (a.index() != b.index()) {
    return false;
  }
  if (std::holds_alternative<std::monostate>(a)) {
    return true;
  } else if (std::holds_alternative<bool>(a)) {
    return std::get<bool>(a) == std::get<bool>(b);
  } else if (std::holds_alternative<double>(a)) {
    return std::get<double>(a) == std::get<double>(b);
  } else if (std::holds_alternative<std::shared_ptr<Obj>>(a)) {
    auto aptr = std::get<std::shared_ptr<Obj>>(a);
    auto bptr = std::get<std::shared_ptr<Obj>>(b);
    // Because all strings are interned, pointer equality is sufficient
    return aptr == bptr;
  } else {
    throw std::runtime_error("unreachable in is_equal: unknown value type");
  }
}

Value add(const Value& a, const Value& b, StringMap& string_map) {
  if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
    return std::get<double>(a) + std::get<double>(b);
  } else if (std::holds_alternative<std::shared_ptr<Obj>>(a) &&
             std::holds_alternative<std::shared_ptr<Obj>>(b)) {
    auto aptr = std::get<std::shared_ptr<Obj>>(a);
    auto bptr = std::get<std::shared_ptr<Obj>>(b);
    return aptr->add(bptr, string_map);
  }
  return false;
}

} // namespace lox
