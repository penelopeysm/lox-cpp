#include "vm.hpp"
#include "chunk.hpp"

#include <iostream>
#include <stdexcept>

#define LOX_DEBUG

namespace lox {
VM::VM(Chunk& chunk) : chunk(chunk), ip(0) { stack.reserve(MAX_STACK_SIZE); }

uint8_t VM::read_byte() { return chunk.at(ip++); }

lox::Value VM::read_constant() {
  uint8_t constant_index = read_byte();
  return chunk.constant_at(constant_index);
}

VM& VM::stack_reset() {
  stack_ptr = 0;
  return *this;
}

VM& VM::stack_push(const lox::Value& value) {
  if (stack_ptr >= MAX_STACK_SIZE) {
    throw std::runtime_error("loxc: stack overflow");
  }
  if (stack_ptr >= stack.size()) {
    stack.push_back(value);
  } else {
    stack[stack_ptr] = value;
  }
  stack_ptr++;
  return *this;
}

lox::Value VM::stack_pop() {
  if (stack_ptr == 0) {
    throw std::runtime_error("loxc: stack underflow");
  }
  stack_ptr--;
  return stack[stack_ptr];
}

std::ostream& VM::stack_dump(std::ostream& out) const {
  if (stack_ptr == 0) {
    out << "          <empty stack>\n";
    return out;
  }
  out << "          ";
  for (size_t i = 0; i < stack_ptr; i++) {
    out << "[" << stack[i] << "]";
  }
  out << "\n";
  return out;
}

InterpretResult VM::run() {
  while (true) {
#ifdef LOX_DEBUG
    stack_dump(std::cout);
    chunk.disassemble(std::cout, ip);
#endif
    uint8_t instruction = read_byte();
    switch (static_cast<OpCode>(instruction)) {
    case OpCode::RETURN: {
      // Don't have actual return values yet, so just print the top of the stack
      std::cout << "returning " << stack_pop() << "\n";
      return InterpretResult::OK;
    }
    case OpCode::CONSTANT: {
      lox::Value c = read_constant();
      stack_push(c);
      continue;
    }
    }
  }
  throw std::runtime_error("loxc: reached unreachable code in VM::run");
}

} // namespace lox
