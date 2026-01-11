#pragma once

#include "compiler.hpp"
#include "value.hpp"
#include <cstddef>
#include <functional>
#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

namespace lox {

InterpretResult interpret(std::string_view source);

class CallFrame {
public:
  std::shared_ptr<ObjFunction> function;
  size_t ip;
  size_t stack_start;

  CallFrame(std::shared_ptr<ObjFunction> fn, size_t ip, size_t stack_start)
      : function(std::move(fn)), ip(ip), stack_start(stack_start) {}
  uint8_t read_byte() { return function->chunk.at(ip++); }
  void shift_ip(int offset) { ip += offset; }
  size_t get_current_debuginfo_line() {
    return function->chunk.debuginfo_at(ip - 1);
  }
  bool is_at_end() const { return ip >= function->chunk.size(); }
  bool exactly_at_end() const { return ip == function->chunk.size(); }

  void disassemble(std::ostream& out) const {
    function->chunk.disassemble(out, ip);
  }
};

class VM {
public:
  VM(std::unique_ptr<scanner::Scanner> scanner, StringMap& interned_strings);
  InterpretResult run();
  std::ostream& stack_dump(std::ostream& out) const;
  InterpretResult invoke_toplevel();

private:
  std::vector<CallFrame> call_frames;
  size_t call_frame_ptr;
  // NOTE: We use std::vector here instead of std::stack because the latter does
  // not provide random access (you can only access the top element).
  std::vector<lox::Value> stack;
  size_t stack_ptr;
  StringMap& interned_strings;
  // maps from the name of a global variable to its value
  std::unordered_map<std::string, lox::Value> globals;
  std::unique_ptr<Parser> parser;

  CallFrame& current_frame() { return call_frames[call_frame_ptr - 1]; }
  Chunk& get_chunk() { return current_frame().function->chunk; }

  lox::Value get_local_variable(size_t local_index) {
    // Because local_index is an index into the current function's locals,
    // which may not start at zero (but instead start at
    // current_frame().stack_start), we need to offset it by stack_start.
    size_t stack_index = current_frame().stack_start + local_index;
    if (stack_index >= stack_ptr) {
      error("get_local_variable: invalid local variable index");
    }
    return stack[stack_index];
  }
  void set_local_variable(size_t local_index, const lox::Value& value) {
    size_t stack_index = current_frame().stack_start + local_index;
    if (stack_index >= stack_ptr) {
      error("set_local_variable: invalid local variable index");
    }
    stack[stack_index] = value;
  }

  // Read a byte and advance the instruction pointer.
  uint8_t read_byte() { return current_frame().read_byte(); }
  // Read a constant using the current byte, and advance the instruction
  // pointer.
  lox::Value read_constant();
  // Read the name of a global variable from the chunk's constant table.
  std::string read_global_name();

  void error(const std::string& message);

  VM& handle_binary_op(const std::function<lox::Value(double, double)>& op);

  void call(std::shared_ptr<ObjFunction> callee, size_t arg_count);

  // Move the stack pointer back to the base
  VM& stack_reset();
  VM& stack_push(const lox::Value& value);
  lox::Value stack_peek();
  lox::Value stack_pop();
  // Apply `op` to the top value on the stack, replacing it with the result
  lox::Value
  stack_modify_top(const std::function<lox::Value(const lox::Value&)>& op);
};

} // namespace lox
