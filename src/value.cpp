#include "value.hpp"
#include <iostream>

namespace {
struct LoxValuePrinter {
  std::ostream& os;
  void operator()(std::monostate) { os << "nil"; }
  void operator()(bool b) { os << (b ? "true" : "false"); }
  void operator()(double d) { os << d; }
};
} // namespace

std::ostream& lox::operator<<(std::ostream& os, const lox::Value& value) {
  std::visit(LoxValuePrinter{os}, value);
  return os;
}
