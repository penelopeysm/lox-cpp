#pragma once

#include "chunk.hpp"
#include "value.hpp"
#include <vector>

namespace lox {

constexpr size_t MAX_STACK_SIZE = 256;

enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

class VM {
public:
  VM(Chunk& chunk);
  InterpretResult run();
  std::ostream& stack_dump(std::ostream& out) const;

private:
  Chunk& chunk;
  size_t ip;
  // NOTE: We use std::vector here instead of std::stack because the latter does
  // not provide random access (you can only access the top element).
  std::vector<lox::Value> stack;
  size_t stack_ptr = 0;

  // Read a byte and advance the instruction pointer.
  uint8_t read_byte();
  // Read a constant using the current byte, and advance the instruction
  // pointer.
  lox::Value read_constant();

  // Move the stack pointer back to the base
  VM& stack_reset();
  VM& stack_push(const lox::Value& value);
  lox::Value stack_pop();
};

} // namespace lox
