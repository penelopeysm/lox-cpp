#include "vm.hpp"
#include "chunk.hpp"
#include "compiler.hpp"
#include "stringmap.hpp"

#include <iostream>
#include <stdexcept>

namespace {
size_t get_jump_offset(uint8_t high_byte, uint8_t low_byte) {
  return (static_cast<size_t>(high_byte) << 8) | low_byte;
}
} // namespace

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

lox::Value VM::stack_peek() {
  if (stack_ptr == 0) {
    error("stack_peek: stack underflow");
  }
  return stack[stack_ptr - 1];
}

std::string VM::read_global_name() {
  lox::Value var_name_value = read_constant();
  // This is technically unsafe since the constant could be any Value, but
  // by construction of the parser it should always be a string.
  auto var_name_ptr = std::get<std::shared_ptr<Obj>>(var_name_value);
  auto var_name_str = std::dynamic_pointer_cast<ObjString>(var_name_ptr);
  if (var_name_str == nullptr) {
    throw std::runtime_error(
        "unreachable: global variable name is not a string");
  }
  return var_name_str->value;
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
  size_t nbytes = chunk.size();
  while (ip < nbytes) {
#ifdef LOX_DEBUG
    stack_dump(std::cerr);
    chunk.disassemble(std::cerr, ip);
#endif
    uint8_t instruction = read_byte();
    switch (static_cast<OpCode>(instruction)) {
    case OpCode::RETURN: {
      // Don't have actual return values yet, so just print the top of the stack
      std::cerr << "returning " << stack_pop() << "\n";
      return InterpretResult::OK;
    }
    case OpCode::CONSTANT: {
      lox::Value c = read_constant();
      stack_push(c);
      break;
    }
    case OpCode::NEGATE: {
      lox::Value value = stack_pop();
      if (std::holds_alternative<double>(value)) {
        stack_push(-std::get<double>(value));
      } else {
        error("operand must be a number");
      }
      break;
    }
    case OpCode::NOT: {
      stack_modify_top(
          [](const lox::Value& value) { return !(lox::is_truthy(value)); });
      break;
    }
    case OpCode::ADD: {
      lox::Value b = stack_pop();
      lox::Value a = stack_pop();
      stack_push(lox::add(a, b, interned_strings));
      break;
    }
    case OpCode::SUBTRACT: {
      handle_binary_op([](double a, double b) { return a - b; });
      break;
    }
    case OpCode::MULTIPLY: {
      handle_binary_op([](double a, double b) { return a * b; });
      break;
    }
    case OpCode::DIVIDE: {
      handle_binary_op([](double a, double b) { return a / b; });
      break;
    }
    case OpCode::EQUAL: {
      lox::Value b = stack_pop();
      lox::Value a = stack_pop();
      stack_push(lox::is_equal(a, b));
      break;
    }
    case OpCode::GREATER: {
      handle_binary_op([](double a, double b) { return a > b; });
      break;
    }
    case OpCode::LESS: {
      handle_binary_op([](double a, double b) { return a < b; });
      break;
    }
    case OpCode::PRINT: {
      lox::Value value = stack_pop();
      // operator<< on Value is already defined to print the correct
      // representation
      std::cout << value << "\n";
      break;
    }
    case OpCode::POP: {
      stack_pop();
      break;
    }
    case OpCode::DEFINE_GLOBAL: {
      // The parser will have pushed the variable name (as a string) onto the
      // chunk's constant table. Separately, in the bytecode, it will have a
      // DEFINE_GLOBAL instruction followed by the constant index. When we get
      // here we have already seen the DEFINE_GLOBAL instruction, so we need to
      // read the variable name.
      std::string var_name = read_global_name();
      // After this, the parser will have emitted bytecode that pushes the value
      // of the variable onto the stack. So we need to pop that value. (Or,
      // following the book, just peek it now and pop it later, after we've
      // stored it.)
      lox::Value var_value = stack_peek();
      // Then we can define the global variable by adding it to our map.
      size_t global_index = chunk.push_constant(var_value);
      // NOTE: operator[] will create a new entry if the key doesn't exist yet.
      // It returns a reference to the value, which can then be assigned to.
      // So this doesn't desugar to something like setindex! in Julia, it is
      // just a composition of operator[] and assignment.
      globals_indices[var_name] = global_index;
      stack_pop(); // now pop the value
      break;
    }
    case OpCode::GET_GLOBAL: {
      std::string var_name = read_global_name();
      // Now that we have the name of the variable, we can look it up in our map
      auto it = globals_indices.find(var_name);
      if (it == globals_indices.end()) {
        error("undefined variable '" + var_name + "'");
      }
      size_t global_index = it->second;
      lox::Value var_value = chunk.constant_at(global_index);
      stack_push(var_value);
      break;
    }
    case OpCode::SET_GLOBAL: {
      std::string var_name = read_global_name();
      auto it = globals_indices.find(var_name);
      if (it == globals_indices.end()) {
        error("undefined variable '" + var_name + "'");
      } else {
        // Update the value in the chunk's constant table.
        // Peek not pop because assignment expressions return the assigned value
        // and it might be used again later!
        lox::Value var_value = stack_peek();
        size_t new_global_index = chunk.push_constant(var_value);
        // Update the mapping to point to the new constant index
        globals_indices[var_name] = new_global_index;
      }
      break;
    }
    case OpCode::SET_LOCAL: {
      uint8_t local_index = read_byte();
      if (static_cast<size_t>(local_index) > stack_ptr) {
        std::cerr << "stack_ptr=" << stack_ptr
                  << ", local_index=" << +local_index << "\n";
        error("SET_LOCAL: invalid local variable index");
      }
      stack[local_index] = stack_peek();
      break;
    }
    case OpCode::GET_LOCAL: {
      uint8_t local_index = read_byte();
      if (static_cast<size_t>(local_index) > stack_ptr) {
        std::cerr << "stack_ptr=" << stack_ptr
                  << ", local_index=" << +local_index << "\n";
        error("GET_LOCAL: invalid local variable index");
      }
      lox::Value local_value = stack[local_index];
      stack_push(local_value);
      break;
    }
    case OpCode::JUMP_IF_FALSE: {
      // Don't pop the condition yet, because we might need to use it for
      // logical shortcircuiting later.
      lox::Value condition = stack_peek();
      if (!lox::is_truthy(condition)) {
        uint8_t high_byte = read_byte();
        uint8_t low_byte = read_byte();
        size_t jump_offset = get_jump_offset(high_byte, low_byte);
        ip += jump_offset;
      } else {
        ip += 2; // skip the jump offset bytes
      }
      break;
    }
    case OpCode::JUMP: {
      uint8_t high_byte = read_byte();
      uint8_t low_byte = read_byte();
      size_t jump_offset = get_jump_offset(high_byte, low_byte);
      ip += jump_offset;
      break;
    }
    case OpCode::LOOP: {
      throw std::runtime_error("unimplemented: LOOP opcode");
      break;
    }
    }
  }
  if (ip == nbytes) {
    // Successfully read all bytes.
    return InterpretResult::OK;
  } else {
    throw std::runtime_error("unexpected end of bytecode");
  }
}

} // namespace lox
