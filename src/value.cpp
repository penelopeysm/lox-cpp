#include "value.hpp"
#include "gc.hpp"
#include "value_def.hpp"
#include <ostream>

namespace lox {

bool operator==(const Upvalue& a, const Upvalue& b) {
  return a.index == b.index && a.is_local == b.is_local;
}

std::ostream& operator<<(std::ostream& os, const Value& value) {
  if (is_nil(value)) {
    os << "nil";
  } else if (is_bool(value)) {
    os << (as_bool(value) ? "true" : "false");
  } else if (is_double(value)) {
    os << as_double(value);
  } else if (is_obj(value)) {
    os << as_obj(value)->to_repr();
  } else {
    throw std::runtime_error("loxc: operator<<: unknown value type");
  }
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
  if (is_nil(value)) {
    return false;
  } else if (is_bool(value)) {
    return as_bool(value);
  } else if (is_double(value)) {
    // all Lox numbers are truthy (even 0)
    return true;
  } else if (is_obj(value)) {
    // everything else is also truthy (even empty string)
    return true;
  } else {
    throw std::runtime_error("loxc: is_truthy: unknown value type");
  }
}

bool is_equal(const Value& a, const Value& b) {
  if (is_nil(a)) {
    return is_nil(b);
  } else if (is_bool(a)) {
    return is_bool(b) && (as_bool(a) == as_bool(b));
  } else if (is_double(a)) {
    return is_double(b) && (as_double(a) == as_double(b));
  } else if (is_obj(a)) {
    // Because all strings are interned, pointer equality is sufficient
    return is_obj(b) && (as_obj(a) == as_obj(b));
  } else {
    throw std::runtime_error("unreachable in is_equal: unknown value type");
  }
}

Value add(const Value& a, const Value& b, GC& gc) {
  if (is_double(a) && is_double(b)) {
    return as_double(a) + as_double(b);
  } else if (is_obj(a) && is_obj(b)) {
    auto aptr = as_obj(a);
    auto bptr = as_obj(b);
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
