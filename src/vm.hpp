#pragma once

#include "compiler.hpp"
#include "stringmap.hpp"
#include "gc.hpp"
#include "value.hpp"
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
    closure->function->chunk.disassemble(out, ip, closure->function->name);
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

  CallFrame& current_frame() { return call_frames.back(); }
  Chunk& get_chunk() { return current_frame().closure->function->chunk; }

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

  // Read a byte and advance the instruction pointer.
  uint8_t read_byte() { return current_frame().read_byte(); }
  // Read a constant using the current byte, and advance the instruction
  // pointer.
  lox::Value read_constant();
  // Read the name of a global variable from the chunk's constant table, and
  // advance the instruction pointer.
  lox::ObjString* read_constant_string();

  void error(const std::string& message);

  void call(ObjClosure* callee, size_t arg_count);

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
    if (std::holds_alternative<double>(a) &&
        std::holds_alternative<double>(b)) {
      lox::Value result = op(std::get<double>(a), std::get<double>(b));
      stack_replace_top(result);
    } else {
      error("operands must be numbers");
    }
    return *this;
  }
};

} // namespace lox
