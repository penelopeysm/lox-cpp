#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "stringmap.hpp"

#include <iostream>
#include <stdexcept>

namespace lox {

lox::InterpretResult interpret(std::string_view source) {
  // Instantiates a scanner that holds the source code; but doesn't actually
  // perform any scanning. Scanning will happen on demand!
  std::unique_ptr<scanner::Scanner> scanner =
      std::make_unique<scanner::Scanner>(source);
  Chunk chunk;
  // Create a map for string interning
  StringMap string_map;
  Parser parser(std::move(scanner), chunk, string_map);
  parser.parse();

  if (parser.error_occurred()) {
    // Print the error
    parser.report_error();
    return InterpretResult::COMPILE_ERROR;
  } else {
    // TODO this creates a copy
    Chunk compiledChunk = parser.get_chunk();
    VM vm(compiledChunk, string_map);
    try {
      vm.run();
    } catch (const std::runtime_error& e) {
      std::cerr << "lox runtime error: " << e.what() << "\n";
      return InterpretResult::RUNTIME_ERROR;
    }
    return InterpretResult::OK;
  }
}

VM::VM(Chunk& chunk, StringMap& interned_strings)
    : chunk(chunk), ip(0), interned_strings(interned_strings) {
  stack.reserve(MAX_STACK_SIZE);
}

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
    error("stack overflow");
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
    error("stack_pop: stack underflow");
  }
  stack_ptr--;
  return stack[stack_ptr];
}

lox::Value
VM::stack_modify_top(const std::function<lox::Value(const lox::Value&)>& op) {
  if (stack_ptr == 0) {
    error("stack_modify_top: stack underflow");
  }
  stack[stack_ptr - 1] = op(stack[stack_ptr - 1]);
  return stack[stack_ptr - 1];
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

void VM::error(const std::string& message) {
  // Use the chunk's debuginfo to figure out which line the error is at.
  size_t line = chunk.debuginfo_at(ip - 1);
  throw std::runtime_error("line " + std::to_string(line) + ": " + message);
}

VM& VM::handle_binary_op(const std::function<lox::Value(double, double)>& op) {
  lox::Value b = stack_pop();
  lox::Value a = stack_pop();
  if (std::holds_alternative<double>(a) && std::holds_alternative<double>(b)) {
    stack_push(op(std::get<double>(a), std::get<double>(b)));
  } else {
    error("operands must be numbers");
  }
  return *this;
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
    case OpCode::NEGATE: {
      lox::Value value = stack_pop();
      if (std::holds_alternative<double>(value)) {
        stack_push(-std::get<double>(value));
      } else {
        error("operand must be a number");
      }
      continue;
    }
    case OpCode::NOT: {
      stack_modify_top(
          [](const lox::Value& value) { return !(lox::is_truthy(value)); });
      continue;
    }
    case OpCode::ADD: {
      lox::Value b = stack_pop();
      lox::Value a = stack_pop();
      stack_push(lox::add(a, b, interned_strings));
      continue;
    }
    case OpCode::SUBTRACT: {
      handle_binary_op([](double a, double b) { return a - b; });
      continue;
    }
    case OpCode::MULTIPLY: {
      handle_binary_op([](double a, double b) { return a * b; });
      continue;
    }
    case OpCode::DIVIDE: {
      handle_binary_op([](double a, double b) { return a / b; });
      continue;
    }
    case OpCode::EQUAL: {
      lox::Value b = stack_pop();
      lox::Value a = stack_pop();
      stack_push(lox::is_equal(a, b));
      continue;
    }
    case OpCode::GREATER: {
      handle_binary_op([](double a, double b) { return a > b; });
      continue;
    }
    case OpCode::LESS: {
      handle_binary_op([](double a, double b) { return a < b; });
      continue;
    }
    }
  }
  error("reached unreachable code in VM::run");
}

} // namespace lox
