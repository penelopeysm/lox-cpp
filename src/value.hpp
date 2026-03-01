#pragma once

#include "chunk.hpp"
#include "stringmap.hpp"
#include "value_def.hpp"
#include <functional>
#include <iostream>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace lox {

enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

enum class ObjType {
  STRING,
  FUNCTION,
  UPVALUE,
  CLOSURE,
  NATIVE_FUNCTION,
  CLASS,
  INSTANCE,
  BOUND_METHOD
};

// Forward declarations
class GC;

class Upvalue {
public:
  size_t index;
  // Indicates whether it is a local variable in the immediately enclosing
  // function (true), or whether it's even higher up (false). We use this
  // information to figure out where in the stack the variable is.
  bool is_local;
};
bool operator==(const Upvalue& a, const Upvalue& b);

class Obj {
public:
  // For runtime type information.
  ObjType type;
  // For our GC's intrusive linked list. These fields are managed by the GC,
  // not by the Obj itself.
  Obj* next = nullptr;
  bool is_marked = false;
  size_t size = 0;

  // NOTE: Marking a member function as `virtual` means that C++ will force
  // it to use dynamic dispatch (i.e., even if there's an Obj* pointer, it will
  // figure out at runtime which exact derived class it points to, and then
  // call the appropriate member function). If you don't do that, if the pointer
  // is statically typed as Obj*, it will always call Obj's member function,
  // not the version on the derived class.
  //
  // In this case, the destructor has to be marked as virtual to ensure that
  // when you delete a Obj*, it calls the derived class's destructor rather
  // than just Obj's destructor. The latter would lead to memory leaks.
  //
  // We don't set the destructor to pure virtual (= 0) because then derived
  // classes would have to implement their own destructor, which is a faff.
  virtual ~Obj() = default;
  // NOTE: the (= 0) makes this a 'pure virtual' function, meaning that derived
  // classes must implement this function.
  virtual std::string to_repr() const = 0;

  // NOTE: `protected` means this constructor can only be called by derived
  // classes
protected:
  Obj(ObjType type) : type(type) {}
};

class ObjString : public Obj {
public:
  std::string value;
  ObjString(std::string_view str) : Obj(static_type), value(str) {}
  std::string to_repr() const override { return "\"" + value + "\""; }

  static constexpr ObjType static_type = ObjType::STRING;
  static constexpr std::string_view static_type_name = "ObjString";
};

class ObjFunction : public Obj {
public:
  std::string name;
  size_t arity;
  std::vector<Upvalue> upvalues;
  Chunk chunk;
  ObjFunction(std::string_view name, size_t arity)
      : Obj(static_type), name(std::string(name)), arity(arity), chunk() {}

  std::string to_repr() const override { return "<fn " + name + ">"; }

  static constexpr ObjType static_type = ObjType::FUNCTION;
  static constexpr std::string_view static_type_name = "ObjFunction";
};

class ObjUpvalue : public Obj {
public:
  Value* location;
  Value closed; // when the upvalue is closed, we store the value here
  ObjUpvalue(Value* location)
      : Obj(static_type), location(location), closed(std::monostate()) {}

  std::string to_repr() const override { return "<upvalue>"; }

  static constexpr ObjType static_type = ObjType::UPVALUE;
  static constexpr std::string_view static_type_name = "ObjUpvalue";
};

class ObjClosure : public Obj {
public:
  ObjFunction* function;
  std::vector<ObjUpvalue*> upvalues;
  ObjClosure(ObjFunction* function) : Obj(static_type), function(function) {}

  std::string to_repr() const override {
    return "<clos " + function->name + ">";
  }

  static constexpr ObjType static_type = ObjType::CLOSURE;
  static constexpr std::string_view static_type_name = "ObjClosure";
};

class ObjNativeFunction : public Obj {
public:
  ObjNativeFunction(
      std::string_view name, size_t arity,
      std::function<Value(size_t arg_count, const Value* args)> function)
      : Obj(static_type), name(std::string(name)), arity(arity),
        function(function) {}

  Value call(uint8_t arg_count, const Value* args);
  std::string to_repr() const override { return "<native fn " + name + ">"; }

  static constexpr ObjType static_type = ObjType::NATIVE_FUNCTION;
  static constexpr std::string_view static_type_name = "ObjNativeFunction";

private:
  std::string name;
  // The number of arguments it SHOULD take.
  size_t arity;
  // The actual C++ function that implements the native function.
  std::function<Value(size_t arg_count, const Value* args)> function;
};

class ObjClass : public Obj {
public:
  ObjString* name;
  std::unordered_map<ObjString*, ObjClosure*> methods;

  ObjClass(ObjString* name) : Obj(static_type), name(name) {}

  std::string to_repr() const override { return "<class " + name->value + ">"; }

  static constexpr ObjType static_type = ObjType::CLASS;
  static constexpr std::string_view static_type_name = "ObjClass";
};

class ObjInstance : public Obj {
public:
  ObjClass* klass;
  std::unordered_map<ObjString*, Value> fields;

  ObjInstance(ObjClass* klass) : Obj(static_type), klass(klass) {}

  std::string to_repr() const override {
    return "<instance of " + klass->to_repr() + ">";
  }

  static constexpr ObjType static_type = ObjType::INSTANCE;
  static constexpr std::string_view static_type_name = "ObjInstance";
};

class ObjBoundMethod : public Obj {
public:
  ObjInstance* receiver;
  ObjClosure* method;

  ObjBoundMethod(ObjInstance* receiver, ObjClosure* method)
      : Obj(static_type), receiver(receiver), method(method) {}

  std::string to_repr() const override {
    return "<bound method " + method->to_repr() + " of " + receiver->to_repr() +
           ">";
  }

  static constexpr ObjType static_type = ObjType::BOUND_METHOD;
  static constexpr std::string_view static_type_name = "ObjBoundMethod";
};

template <typename T> T* as_objptr(Value value, const std::string& error_msg) {
  if (!std::holds_alternative<Obj*>(value)) {
    throw std::runtime_error(error_msg);
  }
  auto objptr = std::get<Obj*>(value);
  if (objptr->type != T::static_type) {
    throw std::runtime_error(error_msg);
  }
  return static_cast<T*>(objptr);
}

bool is_truthy(const Value& value);
bool is_equal(const Value& a, const Value& b);
Value add(const Value& a, const Value& b, GC& gc);

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
