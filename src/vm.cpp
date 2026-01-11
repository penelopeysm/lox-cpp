#include "vm.hpp"
#include "chunk.hpp"
#include "stringmap.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int16_t get_jump_offset(uint8_t high_byte, uint8_t low_byte) {
  return (static_cast<int16_t>(high_byte) << 8) | low_byte;
}
constexpr size_t MAX_CALL_FRAMES = 64;
constexpr size_t MAX_STACK_SIZE = 64 * UINT8_MAX;
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
  // Create a top-level ObjFunction and invoke it.
  VM vm(std::move(scanner), string_map);
  return vm.invoke_toplevel();
}

VM::VM(std::unique_ptr<scanner::Scanner> scanner, StringMap& interned_strings)
    : call_frame_ptr(0), stack_ptr(0), interned_strings(interned_strings),
      parser() {
  call_frames.reserve(MAX_CALL_FRAMES);
  stack.reserve(MAX_STACK_SIZE);
  auto top_level_fn = std::make_unique<ObjFunction>("#toplevel#", 0);
  parser = std::make_unique<Parser>(std::move(scanner), std::move(top_level_fn),
                                    interned_strings);
}

lox::InterpretResult VM::invoke_toplevel() {
  // parse the top-level source code
  parser->parse();
  std::unique_ptr<ObjFunction> top_level_fn_unique =
      parser->finalise_function();
  // Earlier we reserved stack slot zero for the VM. We have to mirror that
  // here. Annoyingly, at this point we need to convert the unique_ptr to
  // a shared_ptr before we can run it
  std::shared_ptr<ObjFunction> top_level_fn = std::move(top_level_fn_unique);
  stack_push(top_level_fn);
  call(top_level_fn, 0);
  // if finalise_function returned nullptr, there was a compile error
  if (top_level_fn == nullptr) {
    // Parser will already have grumbled, no need to do it again here.
    return InterpretResult::COMPILE_ERROR;
  } else {
    return run();
  }
}

lox::Value VM::read_constant() {
  uint8_t constant_index = read_byte();
  return get_chunk().constant_at(constant_index);
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
  size_t line = current_frame().get_current_debuginfo_line();
  throw std::runtime_error("line " + std::to_string(line) + ": " + message);
}

// Create a new call frame and update the VM's internal state so that we are
// in that new frame. This function doesn't actually RUN the code in the
// function; that's left to the VM loop!
void VM::call(std::shared_ptr<ObjFunction> callee, size_t arg_count) {
  if (call_frame_ptr + 1 >= MAX_CALL_FRAMES) {
    throw std::runtime_error("stack overflow: too many nested function calls");
  }
  // Check arity
  if (callee->arity != arg_count) {
    throw std::runtime_error("expected " + std::to_string(callee->arity) +
                             " arguments but got " + std::to_string(arg_count));
  }
  size_t stack_start = stack_ptr - arg_count - 1;
  CallFrame new_frame(callee, 0, stack_start);
  call_frames.push_back(new_frame);
  call_frame_ptr++;
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
  try {

    while (!current_frame().is_at_end()) {
#ifdef LOX_DEBUG
      stack_dump(std::cerr);
      current_frame().disassemble(std::cerr);
#endif
      uint8_t instruction = read_byte();
      switch (static_cast<OpCode>(instruction)) {
      case OpCode::RETURN: {
        // Don't have actual return values yet, so just print the top of the
        // stack
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
        // The parser will have pushed the variable name (as a string) onto
        // the chunk's constant table. Separately, in the bytecode, it will
        // have a DEFINE_GLOBAL instruction followed by the constant index.
        // When we get here we have already seen the DEFINE_GLOBAL
        // instruction, so we need to read the variable name.
        std::string var_name = read_global_name();
        // After this, the parser will have emitted bytecode that pushes the
        // value of the variable onto the stack. So we need to pop that value.
        // (Or, following the book, just peek it now and pop it later, after
        // we've stored it.)
        lox::Value var_value = stack_peek();
        // Then we can define the global variable by adding it to our map.
        // NOTE: operator[] will create a new entry if the key doesn't exist
        // yet. It returns a reference to the value, which can then be
        // assigned to. So this doesn't desugar to something like setindex! in
        // Julia, it is just a composition of operator[] and assignment.
        globals[var_name] = var_value;
        stack_pop(); // now pop the value
        break;
      }
      case OpCode::GET_GLOBAL: {
        std::string var_name = read_global_name();
        // Now that we have the name of the variable, we can look it up in our
        // map
        auto it = globals.find(var_name);
        if (it == globals.end()) {
          error("undefined variable '" + var_name + "'");
        }
        stack_push(it->second);
        break;
      }
      case OpCode::SET_GLOBAL: {
        std::string var_name = read_global_name();
        auto it = globals.find(var_name);
        if (it == globals.end()) {
          error("undefined variable '" + var_name + "'");
        } else {
          // Update the value in the chunk's constant table.
          // Peek not pop because assignment expressions return the assigned
          // value and it might be used again later!
          lox::Value var_value = stack_peek();
          globals[var_name] = var_value;
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
        set_local_variable(local_index, stack_peek());
        break;
      }
      case OpCode::GET_LOCAL: {
        uint8_t local_index = read_byte();
        if (static_cast<size_t>(local_index) > stack_ptr) {
          std::cerr << "stack_ptr=" << stack_ptr
                    << ", local_index=" << +local_index << "\n";
          error("GET_LOCAL: invalid local variable index");
        }
        lox::Value local_value = get_local_variable(local_index);
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
          int16_t jump_offset = get_jump_offset(high_byte, low_byte);
          current_frame().shift_ip(jump_offset);
        } else {
          current_frame().shift_ip(2); // skip the jump offset bytes
        }
        break;
      }
      case OpCode::JUMP: {
        uint8_t high_byte = read_byte();
        uint8_t low_byte = read_byte();
        size_t jump_offset = get_jump_offset(high_byte, low_byte);
        current_frame().shift_ip(jump_offset);
        break;
      }
      case OpCode::CALL: {
        // This is the number of arguments pushed to the stack.
        uint8_t nargs = read_byte();
        // The function pointer should have been pushed to the stack before
        // the arguments.
        auto maybe_objptr = stack[stack_ptr - 1 - nargs];
        if (!std::holds_alternative<std::shared_ptr<Obj>>(maybe_objptr)) {
          throw std::runtime_error("can only call functions");
        }
        auto maybe_fnptr = std::get<std::shared_ptr<lox::Obj>>(maybe_objptr);
        auto fnptr = std::dynamic_pointer_cast<ObjFunction>(maybe_fnptr);
        if (fnptr == nullptr) {
          throw std::runtime_error("can only call functions");
        } else {
          call(fnptr, nargs);
          // TODO pop the call frame
        }
      }
      }
    }
    if (current_frame().exactly_at_end()) {
      // Successfully read all bytes.
      return InterpretResult::OK;
    } else {
      throw std::runtime_error("unexpected end of bytecode");
    }

  } catch (const std::runtime_error& e) {
    std::cerr << "lox runtime error at line "
              << current_frame().get_current_debuginfo_line() << ": "
              << e.what() << "\n";
    // Print call stack
    for (auto fp = call_frames.rbegin(); fp != call_frames.rend(); ++fp) {
      std::cerr << " in function " << fp->function->name << "\n";
    }
    return InterpretResult::RUNTIME_ERROR;
  }
}

} // namespace lox
