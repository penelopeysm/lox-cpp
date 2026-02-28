#include "vm.hpp"
#include "chunk.hpp"
#include "gc.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
ptrdiff_t get_jump_offset(uint8_t high_byte, uint8_t low_byte) {
  // NOTE: we have to be really careful here about how we do the static_cast!
  // The high and low bytes are encoded including the sign information as the
  // most significant bit of the high byte. So we need to combine them into a
  // single int16_t FIRST, so that the sign information is in the right place
  // (i.e. the most significant bit of the int16_t), and then only convert to
  // ptrdiff_t.
  //
  // If we did
  //   static_cast<ptrdiff_t>(high_byte) << 8 | low_byte
  //
  // the sign bit would not be in the most significant bit of the ptrdiff_t, and
  // so we would end up with a completely different number.
  int16_t jump_offset = (static_cast<int16_t>(high_byte) << 8) | low_byte;
  return static_cast<ptrdiff_t>(jump_offset);
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
  GC gc;
  // Create a top-level ObjFunction and invoke it.
  VM vm(std::move(scanner), std::move(gc));
  vm.define_native("clock", 0, clock_native);
  vm.define_native("sleep", 1, sleep_native);
  return vm.invoke_toplevel();
}

VM::VM(std::unique_ptr<scanner::Scanner> scanner, GC gc)
    : _gc(std::move(gc)), parser() {
  call_frames.reserve(MAX_CALL_FRAMES);
  // NOTE: By reserving memory here we avoid having to reallocate when we call
  // push_back. That means that we don't need to do what the book does by having
  // stack_pop decrement a pointer: we can just make stack_pop call pop_back()
  // on the stack. That allows us to keep track of the stack size using
  // stack.size() rather than a separate variable, which can get out of sync.
  stack.reserve(MAX_STACK_SIZE);
  auto top_level_fn = _gc.alloc<ObjFunction>("#toplevel#", size_t(0));
  parser = std::make_unique<Parser>(std::move(scanner), top_level_fn, _gc);

  // Aggressive GC: run it every time we allocate
  _gc.set_alloc_callback([this]() { this->maybe_gc(); });
}

lox::InterpretResult VM::invoke_toplevel() {
  // parse the top-level source code
  parser->parse();
  ObjFunction* top_level_fn = parser->finalise_function();
  // Earlier we reserved stack slot zero for the VM. We have to mirror that
  // here. We push top_level_fn to the stack first so that it doesn't get
  // cleaned up during the call to _gc.alloc.
  stack_push(top_level_fn);
  ObjClosure* top_level_closure = _gc.alloc<ObjClosure>(top_level_fn);
  stack_pop();
  stack_push(top_level_closure);
  call(top_level_closure, 0);
  // if finalise_function returned nullptr, there was a compile error
  if (top_level_closure == nullptr) {
    // Parser will already have grumbled, no need to do it again here.
    return InterpretResult::COMPILE_ERROR;
  } else {
    auto result = run();
#ifdef LOX_GC_DEBUG
    _gc.list_objects();
#endif
    return result;
  }
}

lox::Value VM::read_constant() {
  uint8_t constant_index = read_byte();
  return get_chunk().constant_at(constant_index);
}

VM& VM::stack_reset() {
  stack.clear();
  return *this;
}

VM& VM::stack_push(const lox::Value& value) {
  if (stack.size() >= MAX_STACK_SIZE) {
    error("stack overflow");
  }
  stack.push_back(value);
  return *this;
}

lox::Value* VM::stack_top_address() {
  if (stack.empty()) {
    error("stack_top_address: stack underflow");
  }
  return &stack.back();
}

lox::Value VM::stack_pop() {
  if (stack.empty()) {
    error("stack_pop: stack underflow");
  }
  Value value = std::move(stack.back());
  stack.pop_back();
  return value;
}

lox::Value VM::stack_peek() {
  if (stack.empty()) {
    error("stack_peek: stack underflow");
  }
  return stack.back();
}

std::string VM::read_global_name() {
  lox::Value var_name_value = read_constant();
  // This is technically unsafe since the constant could be any Value, but
  // by construction of the parser it should always be a string.
  return static_cast<ObjString*>(std::get<Obj*>(var_name_value))->value;
}

void VM::close_upvalues_after(Value* addr) {
  for (auto it = open_upvalues.begin(); it != open_upvalues.end();) {
    // `it` iterates over a vector of ObjUpvalue*, so we have to dereference
    // it to get the ObjUpvalue* itself.
    auto upvalue_ptr = *it;
    if (upvalue_ptr->location >= addr) {
#ifdef LOX_DEBUG
      std::cerr << "closing upvalue at location " << upvalue_ptr->location
                << " with value " << *(upvalue_ptr->location) << "\n";
#endif
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
  if (stack.empty()) {
    error("stack_modify_top: stack underflow");
  }
  Value& value = stack.back();
  value = op(value);
  return value;
}

void VM::stack_replace_top(const lox::Value& new_value) {
  if (stack.empty()) {
    error("stack_replace_top: stack underflow");
  }
  stack.back() = new_value;
}

std::ostream& VM::stack_dump(std::ostream& out) const {
  if (stack.empty()) {
    out << "          <empty stack>\n";
    return out;
  }
  out << "          ";
  for (const auto& v : stack) {
    out << "[" << v << "]";
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
  if (call_frames.size() >= MAX_CALL_FRAMES) {
    throw std::runtime_error("stack overflow: too many nested function calls");
  }
  // Check arity
  if (callee->function->arity != arg_count) {
    throw std::runtime_error("expected " +
                             std::to_string(callee->function->arity) +
                             " arguments but got " + std::to_string(arg_count));
  }
  size_t stack_start = stack.size() - arg_count - 1;
  CallFrame new_frame(callee, 0, stack_start);
  call_frames.push_back(new_frame);
}

VM& VM::define_native(
    const std::string& name, size_t arity,
    std::function<lox::Value(size_t, const lox::Value*)> function) {
  auto native_fn = _gc.alloc<ObjNativeFunction>(name, arity, function);
  globals[name] = native_fn;
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
        // We know that `c` has to be a ObjFunction* here, so we can directly
        // use static_cast instead of checking the ObjType inside (even if it's
        // a bit dangerous)
        auto c_fn = static_cast<ObjFunction*>(std::get<Obj*>(c));
        stack_push(c_fn); // avoid GCing the function while the closure is being
                          // created
        auto c_clos = _gc.alloc<ObjClosure>(c_fn);
        stack_pop();
        // we'll push the closure to the stack first even though it's not
        // complete, to avoid it being GC'd while we're making the upvalues.
        stack_push(c_clos);
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
              ObjUpvalue* upvalue = _gc.alloc<ObjUpvalue>(local_value);
              open_upvalues.push_back(upvalue);
              c_clos->upvalues.push_back(upvalue);
            }
          } else {
            // The upvalue references an upvalue in the parent function, so we
            // can just copy the pointer to that upvalue.
            c_clos->upvalues.push_back(
                current_frame().closure->upvalues.at(index));
          }
        }
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
        // This effectively only closes the upvalue that's at the top of the
        // stack.
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
        lox::Value a = stack_peek();
        if (std::holds_alternative<double>(a) &&
            std::holds_alternative<double>(b)) {
          // Attempts at perf optimisations using Instruments.app... don't think
          // it made much of a difference though.
          stack_replace_top(std::get<double>(a) + std::get<double>(b));
        } else {
          stack_pop(); // pop a
          stack_push(lox::add(a, b, _gc));
        }

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
        if (static_cast<size_t>(local_index) > stack.size()) {
          std::cerr << "stack_size=" << stack.size()
                    << ", local_index=" << +local_index << "\n";
          error("SET_LOCAL: invalid local variable index");
        }
        set_local_variable(local_index, stack_peek());
        break;
      }
      case OpCode::GET_LOCAL: {
        uint8_t local_index = read_byte();
        if (static_cast<size_t>(local_index) > stack.size()) {
          std::cerr << "stack_size=" << stack.size()
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
          ptrdiff_t jump_offset = get_jump_offset(high_byte, low_byte);
          current_frame().shift_ip(jump_offset);
        } else {
          current_frame().shift_ip(2); // skip the jump offset bytes
        }
        break;
      }
      case OpCode::JUMP: {
        uint8_t high_byte = read_byte();
        uint8_t low_byte = read_byte();
        ptrdiff_t jump_offset = get_jump_offset(high_byte, low_byte);
        current_frame().shift_ip(jump_offset);
        break;
      }
      case OpCode::CALL: {
        // This is the number of arguments pushed to the stack.
        uint8_t nargs = read_byte();
        // The function pointer should have been pushed to the stack before
        // the arguments.
        auto maybe_objptr = stack[stack.size() - 1 - nargs];
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
            Value retval =
                native_fnptr->call(nargs, &stack[stack.size() - nargs]);
            // Pop the function and its arguments off the stack.
            stack.resize(stack.size() - nargs - 1);
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

        if (call_frames.size() == 1) {
          // We are about to return from the top level, so we're done executing
          // the entire programme. Pop the top level function off the stack
          // and finish.
          stack_pop();
          return InterpretResult::OK;
        } else {
          // Reset the VM's state to where it was before it entered the current
          // call.
          stack.resize(current_frame().stack_start);
          stack_push(retval);
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

void VM::maybe_gc() {
  if (_gc.should_gc()) {

    // Mark roots as grey.
    for (const auto& v : stack) {
      _gc.mark_as_grey(v);
    }
    for (const auto& [_, global_value] : globals) {
      _gc.mark_as_grey(global_value);
    }
    for (const auto& frame : call_frames) {
      _gc.mark_as_grey(frame.closure);
    }
    for (const auto& upvalue : open_upvalues) {
      _gc.mark_as_grey(upvalue);
    }
    parser->mark_function_as_grey();

    _gc.gc();
  }
}

} // namespace lox
