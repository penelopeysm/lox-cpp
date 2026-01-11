#pragma once

#include "chunk.hpp"
#include "value_def.hpp"
#include <iostream>
#include <memory>
#include <ostream>

namespace lox {

enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// Forward declarations
class StringMap; // Actually in stringmap.hpp

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
  virtual Value add(const std::shared_ptr<Obj>& other,
                    StringMap& string_map) = 0;
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

class ObjFunction : public Obj {
public:
  std::string name;
  size_t arity;
  Chunk chunk;

  ObjFunction(std::string_view name, int arity)
      : name(std::string(name)), arity(arity), chunk() {}
  std::string to_repr() const override { return "<fn " + name + ">"; }
  // NOTE: If you aren't using the parameters, you can just omit their names!
  Value add(const std::shared_ptr<Obj>&, StringMap&) override {
    throw std::runtime_error("loxc: add: cannot add function objects");
  }
};

class ObjNativeFunction : public Obj {
public:
  ObjNativeFunction(
      std::string_view name, size_t arity,
      std::function<Value(size_t arg_count, const Value* args)> function)
      : name(std::string(name)), arity(arity), function(function) {}

  Value call(uint8_t arg_count, const Value* args);
  std::string to_repr() const override { return "<native fn " + name + ">"; }
  Value add(const std::shared_ptr<Obj>&, StringMap&) override {
    throw std::runtime_error("loxc: add: cannot add function objects");
  }

private:
  std::string name;
  // The number of arguments it SHOULD take.
  size_t arity;
  // The actual C++ function that implements the native function.
  std::function<Value(size_t arg_count, const Value* args)> function;
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
