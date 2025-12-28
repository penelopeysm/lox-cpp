#pragma once

#include <ostream>
#include <variant>

namespace lox {

enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// NOTE: `using` is similar to `typedef`, but more powerful in C++
using Value = std::variant<std::monostate, bool, double>;

// NOTE: Operators like these should (best) be declared in the same namespace
// as the type they operate on. The compiler will be able to find them via
// 'argument-dependent lookup' (ADL). Note that there is no one single
// `operator<<` function: `std::operator<<` and `lox::operator<<` are different
// functions. Defining it inside the namespace means that we are adding a new
// overload of `lox::operator<<`.
// You CAN define this outside the namespace, in which it would be an overload
// for `std::operator<<`. However, to use this overload from other code inside
// the `lox` namespace, you would either have to explicitly qualify it
//    std::operator<<(os, value);
// or bring it into scope with `using ::operator<<;`.
// It's better to define it inside the namespace and then let the compiler
// figure it out via ADL.
std::ostream &operator<<(std::ostream &os, const Value &value);

} // namespace lox

