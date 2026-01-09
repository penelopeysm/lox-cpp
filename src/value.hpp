#pragma once

#include <iostream>
#include <memory>
#include <ostream>
#include <variant>

namespace lox {

enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// Forward declarations
class Obj;
class StringMap; // Actually in stringmap.hpp

// NOTE: `using` is similar to `typedef`, but more powerful in C++
using Value = std::variant<std::monostate, bool, double, std::shared_ptr<Obj>>;

class Obj {
public:
  // NOTE: Marking a member function as `virtual` means that C++ will force
  // it to use dynamic dispatc (i.e., even if there's an Obj* pointer, it will
  // figure out at runtime which exact derived class it points to, and then
  // call the appropriate member function). If you don't do that, if the pointer
  // is statically typed as Obj*, it will always call Obj's member function,
  // not the version on the derived class.
  // In this case, the destructor has to be marked as virtual to ensure that
  // when you delete a std::shared_ptr<Obj>, it calls the derived class's
  // destructor rather than just Obj's destructor. The latter would lead to
  // memory leaks.
  // We don't set the destructor to pure virtual (= 0) because then derived
  // classes would have to implement their own destructor, which is a faff.
  virtual ~Obj() = default;
  // NOTE: the (= 0) makes this a 'pure virtual' function, meaning that derived
  // classes must implement this function.
  virtual std::string to_repr() const = 0;
  virtual Value add(const std::shared_ptr<Obj>& other, StringMap& string_map) = 0;
};

class ObjString : public Obj {
public:
  std::string value;

  ObjString(std::string_view str) : value(std::string(str)) {}
#ifdef LOX_DEBUG
  ~ObjString() override {
    std::cerr << "ObjString destructor called for \"" << value << "\"\n";
  }
#endif
  std::string to_repr() const override { return "\"" + value + "\""; }
  Value add(const std::shared_ptr<Obj>& other, StringMap& string_map) override;
};

bool is_truthy(const Value& value);
bool is_equal(const Value& a, const Value& b);
Value add(const Value& a, const Value& b, StringMap& string_map);

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
std::ostream& operator<<(std::ostream& os, const Value& value);

} // namespace lox
