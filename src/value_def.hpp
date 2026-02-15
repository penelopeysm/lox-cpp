#pragma once
#include <variant>

namespace lox {
// NOTE: `using` is similar to `typedef`, but more powerful in C++
class Obj;
using Value = std::variant<std::monostate, bool, double, Obj*>;
} // namespace lox
