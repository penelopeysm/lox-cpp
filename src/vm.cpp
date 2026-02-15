#include "vm.hpp"
#include "chunk.hpp"
#include "gc.hpp"
#include "stringmap.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
int16_t get_jump_offset(uint8_t high_byte, uint8_t low_byte) {
  return (static_cast<int16_t>(high_byte) << 8) | low_byte;
}
constexpr size_t MAX_CALL_FRAMES = 64;
constexpr size_t MAX_STACK_SIZE = 64 * UINT8_MAX;

lox::Value clock_native(size_t, const lox::Value*) {
  return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
}
lox::Value sleep_native(size_t, const lox::Value* args) {
  if (!std::holds_alternative<double>(args[0])) {
    throw std::runtime_error("sleep expects one numeric argument");
  }
  double seconds = std::get<double>(args[0]);
  if (seconds < 0) {
    throw std::runtime_error("sleep duration must be non-negative");
  }
  std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
  return lox::Value{}; // return nil
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
  // Create a top-level ObjFunction and invoke it.
  VM vm(std::move(scanner), string_map);
  vm.define_native("clock", 0, clock_native);
  vm.define_native("sleep", 1, sleep_native);
  return vm.invoke_toplevel();
}

VM::VM(std::unique_ptr<scanner::Scanner> scanner, StringMap& interned_strings)
    : call_frame_ptr(0), stack_ptr(0), interned_strings(interned_strings),
      parser() {
  call_frames.reserve(MAX_CALL_FRAMES);
  stack.reserve(MAX_STACK_SIZE);
  auto top_level_fn = gc_new<ObjFunction>("#toplevel#", 0);
  parser = std::make_unique<Parser>(std::move(scanner), top_level_fn,
                                    interned_strings);
}

lox::InterpretResult VM::invoke_toplevel() {
  // parse the top-level source code
  parser->parse();
  ObjFunction* top_level_fn = parser->finalise_function();
  // Earlier we reserved stack slot zero for the VM. We have to mirror that
  // here. Annoyingly, at this point we need to convert the unique_ptr to
  // a shared_ptr before we can run it
  ObjClosure* top_level_closure = gc_new<ObjClosure>(top_level_fn);
  stack_push(top_level_closure);
  call(top_level_closure, 0);
  // if finalise_function returned nullptr, there was a compile error
  if (top_level_closure == nullptr) {
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

lox::Value* VM::stack_top_address() {
  if (stack_ptr == 0) {
    error("stack_top_address: stack underflow");
  }
  return &stack[stack_ptr - 1];
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
  return static_cast<ObjString*>(std::get<Obj*>(var_name_value))->value;
}

void VM::close_upvalues_after(Value* addr) {
  for (auto it = open_upvalues.begin(); it != open_upvalues.end();) {
    // `it` is an iterator over a vector of ObjUpvalue*, so `it` itself is
    // ObjUpvalue**
    auto upvalue_ptr = *it;
    if (upvalue_ptr->location >= addr) {
      std::cerr << "closing upvalue at location " << upvalue_ptr->location
                << " with value " << *(upvalue_ptr->location) << "\n";
      upvalue_ptr->closed = *(upvalue_ptr->location);
      upvalue_ptr->location = &(upvalue_ptr->closed);
      it = open_upvalues.erase(it);
    } else {
      ++it;
    }
  }
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
void VM::call(ObjClosure* callee, size_t arg_count) {
  if (call_frame_ptr >= MAX_CALL_FRAMES) {
    throw std::runtime_error("stack overflow: too many nested function calls");
  }
  // Check arity
  if (callee->function->arity != arg_count) {
    throw std::runtime_error("expected " +
                             std::to_string(callee->function->arity) +
                             " arguments but got " + std::to_string(arg_count));
  }
  size_t stack_start = stack_ptr - arg_count - 1;
  CallFrame new_frame(callee, 0, stack_start);
  bool is_first_frame = call_frames.empty();
  call_frames.push_back(new_frame);
  if (!is_first_frame) {
    // For the first (toplevel) frame, we already have call_frame_ptr at 0,
    // so we don't need to increment it.
    call_frame_ptr++;
  }
}

VM& VM::define_native(
    const std::string& name, size_t arity,
    std::function<lox::Value(size_t, const lox::Value*)> function) {
  auto native_fn = gc_new<ObjNativeFunction>(name, arity, function);
  globals[name] = native_fn;
  return *this;
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
      case OpCode::CONSTANT: {
        lox::Value c = read_constant();
        stack_push(c);
        break;
      }
      case OpCode::CLOSURE: {
        lox::Value c = read_constant();
        // We know that `c` has to be a ObjFunction* here, so we can use
        // static_cast instead of dynamic_cast (even if it's a bit dangerous)
        auto c_fn = static_cast<ObjFunction*>(std::get<Obj*>(c));
        auto c_clos = gc_new<ObjClosure>(c_fn);
        for (size_t i = 0; i < c_fn->upvalues.size(); ++i) {
          uint8_t is_local = read_byte();
          uint8_t index = read_byte();
          if (is_local) {
            // The upvalue references a local variable in its parent function
            // (which is the current function! since we have just finished
            // compiling the inner function and have now exited back to the
            // parent).
            Value* local_value = get_local_variable_address(index);
            // local_value is somewhere on the stack. Let's check if the VM
            // already has an open upvalue pointing to that stack slot. If so,
            // we can reuse it.
            // NOTE: lambda captures are kind of weird in C++! You can't use
            // local_value inside the lambda unless you capture it by
            // including it inside the square brackets (which is a capture
            // list). Then there are also lots of different ways to capture
            // things. You can capture by reference ([&something]) or by
            // value ([something]), and you can also capture *everything* by
            // reference ([&]) or by value ([=]).
            // TODO: this is a bit suboptimal because our open_upvalues
            // vector is not sorted. That means that every search is O(n). We
            // could optimise this by keeping open_upvalues sorted and then
            // stopping once we have passed the relevant stack slot. The book
            // does this, but I didn't want to bother with the manual pointer
            // stuff.
            auto it = std::find_if(open_upvalues.begin(), open_upvalues.end(),
                                   [local_value](ObjUpvalue* upv) {
                                     return upv->location == local_value;
                                   });
            if (it != open_upvalues.end()) {
              c_clos->upvalues.push_back(*it);
            } else {
              // Not found so we can create a new upvalue
              ObjUpvalue* upvalue = gc_new<ObjUpvalue>(local_value);
              open_upvalues.push_back(upvalue);
              c_clos->upvalues.push_back(upvalue);
            }
          }
        }
        stack_push(c_clos);
        break;
      }
      case OpCode::GET_UPVALUE: {
        uint8_t upvalue_index = read_byte();
        lox::ObjUpvalue* upvalue =
            current_frame().closure->upvalues.at(upvalue_index);
        lox::Value actual_value = *(upvalue->location);
        stack_push(actual_value);
        break;
      }
      case OpCode::SET_UPVALUE: {
        uint8_t upvalue_index = read_byte();
        lox::ObjUpvalue* upvalue =
            current_frame().closure->upvalues.at(upvalue_index);
        lox::Value target_value = stack_peek();
        *(upvalue->location) = target_value;
        break;
      }
      case OpCode::CLOSE_UPVALUE: {
        close_upvalues_after(stack_top_address());
        stack_pop();
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
        if (!std::holds_alternative<Obj*>(maybe_objptr)) {
          throw std::runtime_error("objptr was not a pointer to Obj");
        }
        auto maybe_closptr = std::get<Obj*>(maybe_objptr);
        auto closptr = dynamic_cast<ObjClosure*>(maybe_closptr);
        if (closptr != nullptr) {
          call(closptr, nargs);
        } else {
          auto native_fnptr = dynamic_cast<ObjNativeFunction*>(maybe_closptr);
          if (native_fnptr != nullptr) {
            Value retval = native_fnptr->call(nargs, &stack[stack_ptr - nargs]);
            // Pop the function and its arguments off the stack.
            stack_ptr -= (nargs + 1);
            // Push the return value onto the stack.
            stack_push(retval);
          } else {
            throw std::runtime_error(
                "can only call closure or native function");
          }
        }
        break;
      }
      case OpCode::RETURN: {
        lox::Value retval = stack_pop();
        close_upvalues_after(&stack[current_frame().stack_start]);
        // std::cerr << "returning " << retval << "\n";
        // Pop the current call frame.
        if (call_frame_ptr == 0) {
          // Returned from the top level, so we're done executing the entire
          // programme. Pop the top level function off the stack and finish.
          stack_pop();
          return InterpretResult::OK;
        } else {
          // Reset the VM's state to where it was before it entered the current
          // call.
          stack_ptr = current_frame().stack_start;
          stack_push(retval);
          --call_frame_ptr;
          call_frames.pop_back();
        }
        break;
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
    for (auto cf = call_frames.rbegin(); cf != call_frames.rend(); ++cf) {
      std::string fname = cf->closure->function->name;
      std::size_t line = cf->get_current_debuginfo_line();
      std::cerr << " in line " << line << ", function " << fname << "\n";
    }
    return InterpretResult::RUNTIME_ERROR;
  }
}

} // namespace lox
