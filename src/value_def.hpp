#pragma once
#include <memory>
#include <variant>

namespace lox {
// NOTE: `using` is similar to `typedef`, but more powerful in C++
class Obj;
using Value = std::variant<std::monostate, bool, double, std::shared_ptr<Obj>>;
} // namespace lox
