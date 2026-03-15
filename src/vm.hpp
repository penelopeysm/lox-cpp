#pragma once

#include "compiler.hpp"
#include "gc.hpp"
#include "stringmap.hpp"
#include "value.hpp"
#include "value_def.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace lox {

InterpretResult interpret(std::string_view source);

class CallFrame {
public:
  ObjClosure* closure;
  size_t ip;
  size_t stack_start;

  CallFrame(ObjClosure* clos, size_t ip, size_t stack_start)
      : closure(std::move(clos)), ip(ip), stack_start(stack_start) {}
  uint8_t read_byte() { return closure->function->chunk.at(ip++); }
  void shift_ip(ptrdiff_t offset) {
    ptrdiff_t ip_signed = static_cast<ptrdiff_t>(ip);
    ip_signed += offset;
    if (ip_signed < 0) {
      throw std::runtime_error("shift_ip: negative offset out of bounds");
    }
    ip = static_cast<size_t>(ip_signed);
    if (ip > closure->function->chunk.size()) {
      throw std::runtime_error("shift_ip: positive offset out of bounds");
    }
  }
  size_t get_current_debuginfo_line() {
    return closure->function->chunk.debuginfo_at(ip - 1);
  }
  bool is_at_end() const { return ip >= closure->function->chunk.size(); }
  bool exactly_at_end() const { return ip == closure->function->chunk.size(); }

  void disassemble(std::ostream& out) const {
    closure->function->chunk.disassemble(out, ip,
                                         closure->function->name->value);
  }
};

class VM {
public:
  VM(std::unique_ptr<scanner::Scanner> scanner, GC gc);
  InterpretResult run();
  std::ostream& stack_dump(std::ostream& out) const;
  InterpretResult invoke_toplevel();
  VM& define_native(
      const std::string& name, size_t arity,
      std::function<lox::Value(size_t, const lox::Value*)> function);

private:
  std::vector<CallFrame> call_frames;
  // NOTE: We use std::vector here instead of std::stack because the latter does
  // not provide random access (you can only access the top element).
  std::vector<lox::Value> stack;
  GC _gc;
  // maps from the name of a global variable to its value
  StringMap<Value> globals;
  std::unique_ptr<Parser> parser;
  // sorted upvalues that haven't been closed yet. They sort in decreasing order
  // of the stack slot that they point to
  std::vector<ObjUpvalue*> open_upvalues;
  // Interned string for "init", which we use when we need to look up the
  // initialiser method of a class.
  ObjString* initString;

  CallFrame& current_frame() { return call_frames.back(); }
  Chunk* get_chunk_ptr() { return &current_frame().closure->function->chunk; }

  void close_upvalues_after(Value* addr);

  // Run garbage collection
  void maybe_gc();

  lox::Value get_local_variable(size_t local_index) {
    // Because local_index is an index into the current function's locals,
    // which may not start at zero (but instead start at
    // current_frame().stack_start), we need to offset it by stack_start.
    size_t stack_index = current_frame().stack_start + local_index;
    if (stack_index >= stack.size()) {
      error("get_local_variable: invalid local variable index");
    }
    return stack[stack_index];
  }

  lox::Value* get_local_variable_address(size_t local_index) {
    size_t stack_index = current_frame().stack_start + local_index;
    if (stack_index >= stack.size()) {
      error("get_local_variable: invalid local variable index");
    }
    return &stack[current_frame().stack_start + local_index];
  }

  void set_local_variable(size_t local_index, const lox::Value& value) {
    size_t stack_index = current_frame().stack_start + local_index;
    if (stack_index >= stack.size()) {
      error("set_local_variable: invalid local variable index");
    }
    stack[stack_index] = value;
  }

  void error(const std::string& message);

  // `dispatch_call` figures out from the type of `callee` exactly what to do,
  // but the actual call is done in `call()`. It returns a bool indicating
  // whether the call frame was changed. (This doesn't always happen! If it's a
  // native function, or a class constructor that isn't an initialiser, there's
  // no new call frame to jump into: the return value is just placed on the
  // stack.)
  // The reason why we need to know whether the call frame was changed is that
  // if it was, then we need to update the local variables like local_ip inside
  // the VM loop. If not, then we don't need to.
  [[nodiscard]] bool dispatch_call(lox::Value callee, size_t arg_count,
                                   uint8_t* local_ip);
  void call(ObjClosure* callee, size_t arg_count, uint8_t* local_ip);

  // Move the stack pointer back to the base
  VM& stack_reset();
  VM& stack_push(const lox::Value& value);
  lox::Value stack_peek();
  lox::Value* stack_top_address();
  lox::Value stack_pop();
  // Apply `op` to the top value on the stack, replacing it with the result
  lox::Value
  stack_modify_top(const std::function<lox::Value(const lox::Value&)>& op);

  void stack_replace_top(const lox::Value&);

  // This used to be
  // VM& handle_binary_op(const std::function<lox::Value(double, double)>& op);
  // but we now make it a template to avoid type erasure
  template <typename BinaryOp> VM& handle_binary_op(BinaryOp op) {
    lox::Value b = stack_pop();
    lox::Value a = stack_peek();
    if (is_double(a) && is_double(b)) {
      lox::Value result = op(as_double(a), as_double(b));
      stack_replace_top(result);
    } else {
      error("operands must be numbers");
    }
    return *this;
  }
};

} // namespace lox
