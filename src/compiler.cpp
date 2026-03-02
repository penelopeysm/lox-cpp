#include "compiler.hpp"
#include "chunk.hpp"
#include "gc.hpp"
#include "scanner.hpp"

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
lox::scanner::Token SENTINEL_EOF =
    lox::scanner::Token(lox::scanner::TokenType::_EOF, "", 0);

std::pair<uint8_t, uint8_t> split_jump_offset(int16_t offset) {
  uint8_t high_byte = static_cast<uint8_t>((offset >> 8) & 0xff);
  uint8_t low_byte = static_cast<uint8_t>(offset & 0xff);
  return {high_byte, low_byte};
}

constexpr int16_t MAX_ARITY = 255;
} // namespace

namespace lox {

Precedence next_precedence(Precedence in) {
  if (in == Precedence::PRIMARY) {
    throw std::runtime_error("unreachable: PRIMARY has the highest precedence");
  }
  return static_cast<Precedence>(static_cast<int>(in) + 1);
}

std::vector<bool> Compiler::end_scope() {
  std::vector<bool> is_captureds;
  while (!locals.empty() && locals.back().depth == scope_depth) {
    bool is_captured = locals.back().is_captured;
    locals.pop_back();
    is_captureds.push_back(is_captured);
  }
  --scope_depth;
  return is_captureds;
}

bool Compiler::declare_local(std::string_view name) {
  if (locals.size() >= 256) {
    throw std::runtime_error("too many local variables in function");
  }
  // Check for duplicates. We go backwards from the end of the locals vector
  // until we find something that has a smaller depth than the current scope
  // (which means that we've rewound until before the current scope began)
  // or we find a duplicate.
  for (auto it = locals.rbegin(); it != locals.rend(); ++it) {
    if (it->depth < scope_depth) {
      break;
    }
    if (it->name == name && it->depth == scope_depth) {
      // oops!
      return true;
    }
  }
  // If we reached here, we're good.
  locals.push_back(Local{scope_depth, name, false});
  return false;
}

std::optional<size_t> Compiler::resolve_local(std::string_view name) {
  for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
    size_t u = static_cast<size_t>(i);
    if (locals[u].name == name) {
      return u;
    }
  }
  return std::nullopt;
}

std::optional<size_t> Compiler::resolve_upvalue(std::string_view name) {
  // If there is no enclosing function, it can't be an upvalue
  if (parent == nullptr)
    return std::nullopt;

  // Let's check if it's a local variable in the immediately enclosing
  // function...
  std::optional<size_t> opt_local_index = parent->resolve_local(name);
  if (opt_local_index.has_value()) {
    size_t parent_local_index = opt_local_index.value();
    parent->locals[parent_local_index].is_captured = true;
    size_t upvalue_index = declare_upvalue(Upvalue{parent_local_index, true});
    return upvalue_index;
  }

  // If not, recurse upwards
  std::optional<size_t> opt_upvalue_index = parent->resolve_upvalue(name);
  if (opt_upvalue_index.has_value()) {
    size_t parent_upvalue_index = opt_upvalue_index.value();
    size_t upvalue_index =
        declare_upvalue(Upvalue{parent_upvalue_index, false});
    return upvalue_index;
  }

  // Not an upvalue, so it's a global.
  return std::nullopt;
}

void Compiler::mark_function_as_grey(GC& _gc) {
  _gc.mark_as_grey(current_function);
  if (parent != nullptr) {
    parent->mark_function_as_grey(_gc);
  }
}

size_t Compiler::declare_upvalue(Upvalue upvalue) {
  // Check if this upvalue has already been declared.
  for (size_t i = 0; i < current_function->upvalues.size(); i++) {
    if (current_function->upvalues[i] == upvalue) {
      return i;
    }
  }
  current_function->upvalues.push_back(upvalue);
  return current_function->upvalues.size() - 1;
}

using lox::scanner::Scanner;
using lox::scanner::TokenType;

Parser::Parser(std::unique_ptr<Scanner> scanner, ObjFunction* fnptr, GC& gc)
    : scanner(std::move(scanner)), current(SENTINEL_EOF),
      previous(SENTINEL_EOF), errmsg(std::nullopt), gc(gc),
      compiler(
          std::make_unique<Compiler>(fnptr, nullptr, FunctionType::TOPLEVEL)),
      current_class(nullptr) {}

void Parser::advance() {
  previous = current;
  current = scanner->scan_token();
  if (current.type == TokenType::ERROR) {
    error(current.lexeme, current.line);
  }
}

void Parser::function(bool is_class_method) {
  // Parse the name and arity of the function: that will let us create the
  // ObjFunction object
  consume_or_error(TokenType::IDENTIFIER, "expected function name");
  std::string_view fn_name = previous.lexeme;
  consume_or_error(TokenType::LEFT_PAREN, "expected '(' after function name");
  // Create the function object and a new compiler for it. The arity will be
  // updated on the fly later when we parse the function parameters.
  size_t arity = 0;
  // TODO: This copies the string. Do we need to?
  auto new_fnptr = gc.alloc<ObjFunction>(fn_name, arity);
  FunctionType fn_type = is_class_method
                             ? (fn_name == "init" ? FunctionType::CLASSINIT
                                                  : FunctionType::CLASSMETHOD)
                             : FunctionType::FUNCTION;
  auto new_compiler =
      std::make_unique<Compiler>(new_fnptr, std::move(compiler), fn_type);
  compiler = std::move(new_compiler);
  compiler->begin_scope();
  // Parse parameters (if there are any).
  if (!consume_if(TokenType::RIGHT_PAREN)) {
    while (true) {
      arity++;
      if (arity > MAX_ARITY) {
        error("cannot have more than 255 parameters", previous.line);
      }
      consume_or_error(TokenType::IDENTIFIER, "expected parameter name");
      std::string_view param_name = previous.lexeme;
      define_variable(param_name);
      if (consume_if(TokenType::COMMA)) {
        // next parameter
        continue;
      } else if (consume_if(TokenType::RIGHT_PAREN)) {
        // no more parameters
        break;
      } else {
        // syntax error
        error("expected ',' or ')' after parameter", previous.line);
        break;
      }
    }
  }
  compiler->set_function_arity(arity);

  // Parse function body
  consume_or_error(TokenType::LEFT_BRACE, "expected '{' before function body");
  block();
  auto final_fnptr = finalise_function();
  // can be nullptr if there was a compile error
  if (final_fnptr == nullptr) {
    return;
  } else {
    size_t constant_index = make_constant(final_fnptr);
    emit(lox::OpCode::CLOSURE);
    emit(static_cast<uint8_t>(constant_index));
    for (const Upvalue& upvalue : final_fnptr->upvalues) {
      emit(upvalue.is_local ? 1 : 0);
      emit(static_cast<uint8_t>(upvalue.index));
    }
    if (!is_class_method) {
      // This makes `fn_name` available either as a local variable (if it's in
      // an inner scope) or a global variable. However, we don't always want to
      // do that: for example if we're parsing a class method, then the
      // function name is not a variable itself, but is instead stored inside
      // the method table. Hence we have a boolean parameter that lets us
      // control whether the variable is defined.
      define_variable(fn_name);
    }
  }
}

void Parser::call(bool) {
  // We've already consumed the '(' token.
  size_t arg_count = 0;
  // The parsing code here is very similar to the parameter parsing code in
  // function(). Because function parameters are just local variables, we have
  // already set aside the space for them on the stack when we defined the
  // function (inside compiler->declare_local). So here we just need to push the
  // arguments onto the stack, which is what expression() does. Simple! Ish.
  if (!consume_if(TokenType::RIGHT_PAREN)) {
    while (true) {
      arg_count++;
      if (arg_count > MAX_ARITY) {
        error("cannot have more than 255 arguments", previous.line);
      }
      expression();
      if (consume_if(TokenType::COMMA)) {
        // next argument
        continue;
      } else if (consume_if(TokenType::RIGHT_PAREN)) {
        // no more arguments
        break;
      } else {
        // syntax error
        error("expected ',' or ')' after argument", previous.line);
        break;
      }
    }
  }
  emit_call(arg_count);
}

void Parser::mark_function_as_grey() {
  // You might ask: Why can `compiler` ever be nullptr? Well, inside
  // `finalise_function` we set `compiler = compiler->get_parent()`. If there
  // is no parent, then compiler can be set to nullptr. What's worse is
  // immediately after finalise_function is called, we have to create a new
  // ObjClosure from the function object we just compiled, and that might
  // trigger GC, which will call this function.
  if (compiler != nullptr) {
    compiler->mark_function_as_grey(gc);
  }
}

ObjFunction* Parser::finalise_function() {
  if (has_error()) {
    std::cerr << "[line " << errmsg->second << "] Error: " << errmsg->first
              << "\n";
    return nullptr;
  } else {
    // Just tack a return at the end of the function body. If there was already
    // one, it won't matter that we do this: it'll be unreachable. But if we
    // fall off the end of a function body, this will catch us.
    emit_auto_return_value();
    emit(lox::OpCode::RETURN);
    // Get the function object from the current compiler, and pop it off the
    // compiler stack.
    auto fnptr = compiler->get_current_function();
    compiler = compiler->get_parent();
    return fnptr;
  }
}

// This will consume `parser.current` if it matches `type` (and that means
// that the consumed token will be stored in `parser.previous`!).
bool Parser::consume_or_error(TokenType type, std::string_view error_message) {
  bool found = current.type == type;
  if (found) {
    advance();
  } else {
    error(error_message, current.line);
  }
  return found;
}

bool Parser::consume_if(TokenType type) {
  bool found = current.type == type;
  if (found) {
    advance();
  }
  return found;
}

void Parser::error(std::string_view message, size_t line) {
  errmsg = std::pair(std::string(message), line);
}

size_t Parser::make_constant(lox::Value value) {
  size_t constant_index = compiler->push_constant(value);
  if (constant_index > UINT8_MAX) {
    error("Too many constants in one chunk.", previous.line);
  }
  return constant_index;
}

size_t Parser::emit_constant(lox::Value value) {
  size_t constant_index = make_constant(value);
  emit(lox::OpCode::CONSTANT);
  emit(static_cast<uint8_t>(constant_index));
  return constant_index;
}

void Parser::emit_call(size_t arg_count) {
  emit(lox::OpCode::CALL);
  emit(static_cast<uint8_t>(arg_count));
}

size_t Parser::emit_jump(lox::OpCode jump_opcode) {
  emit(jump_opcode);
  // Placeholder for the jump offset (2 bytes)
  emit(0xff);
  emit(0xff);
  // Return the index of the first byte of the offset
  return get_chunk_size() - 2;
}

void Parser::patch_jump(size_t jump_byte, size_t target_byte) {
  // we subtract 2 because those are the two bytes that contain the offset, and
  // those get read with read_byte() which increments the instruction pointer
  // already. Use `int` here to catch under/overflow when computing the offset.
  int jump_offset =
      static_cast<int>(target_byte) - static_cast<int>(jump_byte) - 2;
  if (jump_offset > INT16_MAX || jump_offset < INT16_MIN) {
    error("Too much code to jump over.", previous.line);
    return;
  }
  // Split the jump offset into two bytes
  auto [high_byte, low_byte] =
      split_jump_offset(static_cast<int16_t>(jump_offset));
  // Patch the two bytes into the chunk
  compiler->patch_at_offset(jump_byte, high_byte, low_byte);
}

void Parser::parse() {
  advance(); // Load the first token into `current`.
  while (current.type != TokenType::_EOF && !has_error()) {
    declaration();
  }
}

void Parser::parse_precedence(Precedence precedence) {
  advance();
  Rule prefix_rule = get_rule(previous.type);
  if (prefix_rule.prefix == nullptr) {
    error("expected expression", previous.line);
    return;
  }

  bool can_assign = precedence <= Precedence::ASSIGNMENT;
  // Call the prefix parse function.
  // NOTE: std::invoke is a nicer way of calling member function pointers.
  // The alternative is: (this->*prefix_rule.prefix)(can_assign);
  std::invoke(prefix_rule.prefix, this, can_assign);

  // OK, now we parsed a prefix. We need to check if it could be an operand to
  // an infix operator.
  while (true) {
    // The way we do so, is to look at the current token (which could
    // potentially be an infix operator). If its precedence is larger than the
    // current precedence we're parsing, then we need to parse the infix
    // operator.
    Rule next_rule = get_rule(current.type);
    if (next_rule.precedence < precedence) {
      // If not, then it's not an infix operator for us to parse at this level.
      break;
    }
    // Start parsing the next operand.
    advance();
    ParserMemFn infix_rule = next_rule.infix;
    if (infix_rule != nullptr) {
      std::invoke(infix_rule, this, can_assign);
    } else {
      // This really shouldn't happen, because if the precedence is not NONE,
      // then there must be an infix rule. In other words, if it's not a valid
      // infix operator, its precedence should be NONE, and we should have
      // broken out of the loop earlier.
      throw std::runtime_error(
          "unreachable: no infix rule for token " + to_string(current.type) +
          " with precedence " +
          std::to_string(static_cast<int>(next_rule.precedence)));
    }
  }
}

void Parser::declaration() {
  if (consume_if(TokenType::VAR)) {
    var_declaration();
  } else if (consume_if(TokenType::FUN)) {
    function(false);
  } else if (consume_if(TokenType::CLASS)) {
    class_declaration();
  } else {
    statement();
  }
}

void Parser::class_declaration() {
  // class token already consumed
  consume_or_error(TokenType::IDENTIFIER, "expected class name");
  std::string_view class_name = previous.lexeme;

  // Store the class name in the constant table. The CLASS instruction will
  // construct the ObjClass at runtime and put it on the stack.
  uint8_t name_constant_index = make_constant(gc.get_string_ptr(class_name));
  emit(lox::OpCode::CLASS);
  emit(name_constant_index);

  push_current_class();

  // This call will emit code to read from the top of the stack and create
  // either a local or global variable with the class.
  define_variable(class_name);

  // We're now going to force the class to be placed on top of the stack. This
  // is so that when we parse the methods, we can have the class be immediately
  // below the method on the stack, so that the VM knows where to add the
  // methods to.
  named_variable(class_name, false);

  // Now we can attempt to parse the body.
  consume_or_error(TokenType::LEFT_BRACE, "expected '{' before class body");
  while (current.type != TokenType::RIGHT_BRACE && !is_at_end() &&
         !has_error()) {
    method();
  }
  consume_or_error(TokenType::RIGHT_BRACE, "expected '}' after class body");

  // When we're done, we can pop the class off the stack since we don't need it
  // any more.
  emit(lox::OpCode::POP);

  pop_current_class();
}

void Parser::method() {
  // This parses the function body and emits code to create an ObjClosure and
  // put it at the top of the stack.
  function(true);
  // We then need to tell the VM to read the ObjClosure at the top of the stack
  // and store it in the method table of the current class. The book just calls
  // this 'METHOD' but I've chosen to give it a more descriptive name.
  emit(lox::OpCode::DEFINE_METHOD);
  // The book also stores the method name in the constant table, and emits the
  // constant index here. We'll just be lazy and get the VM to read the method
  // name from the ObjClosure itself, since it stores that information anyway.
}

void Parser::var_declaration() {
  // Parse identifier
  consume_or_error(TokenType::IDENTIFIER, "expected variable name");
  std::string_view var_name = previous.lexeme;
  // Initializer. These instructions, when executed, will add the initial value
  // to the top of the stack.
  if (consume_if(TokenType::EQUAL)) {
    expression();
  } else {
    // if no initializer provided, initialize to nil
    emit_constant(std::monostate());
  }
  define_variable(var_name);
  consume_or_error(TokenType::SEMICOLON,
                   "expected ';' after variable declaration");
}

void Parser::define_variable(std::string_view var_name) {
  // Determine whether it's a global or local variable
  bool is_local = compiler->get_scope_depth() > 0;
  if (is_local) {
    // For a local variable, we just need to define it in the compiler, so that
    // next time we refer to it, we know where it is on the stack.
    bool has_duplicate = compiler->declare_local(var_name);
    if (has_duplicate) {
      error("variable '" + std::string(var_name) +
                "' already declared in this scope",
            previous.line);
      return;
    }
  } else {
    // For a global variable, we need to emit a DEFINE_GLOBAL instruction for
    // the VM to pick up.
    define_global_variable(var_name);
  }
}

void Parser::define_global_variable(std::string_view name) {
  ObjString* var_name_str = gc.get_string_ptr(name);
  // NOTE: we use make_constant here (not emit_constant) because we don't want
  // to emit a CONSTANT instruction right now. If we did so then the VM would
  // interpret it as a string literal.
  size_t constant_index = make_constant(var_name_str);
  emit(lox::OpCode::DEFINE_GLOBAL);
  emit(static_cast<uint8_t>(constant_index));
}

void Parser::variable(bool can_assign) {
  // We've already consumed the identifier token, so it's in `previous`.
  named_variable(previous.lexeme, can_assign);
}

void Parser::emit_variable_access(lox::OpCode set_opcode,
                                  lox::OpCode get_opcode, bool can_assign,
                                  size_t index) {
  // check whether it's a get or a set.
  if (consume_if(TokenType::EQUAL)) {
    if (!can_assign) {
      error("invalid assignment target", previous.line);
    }
    // It's a set. So we need to first parse & emit code for the rhs (the
    // value), which will put the value on top of the stack.
    expression();
    // The set opcode will then read it from the stack and assign it to the
    // variable in position `index`. (Exactly what `index` refers to depends on
    // whether it's a local, upvalue, or global. For locals, `index` refers to
    // the position on the stack. For globals and properties, `index` refers to
    // the index of the variable NAME in the constant table. The VM then looks
    // up the name in its globals map to get the value.)
    emit(set_opcode);
    emit(static_cast<uint8_t>(index));
  } else {
    // Just a get; the get opcode will read it from position `index` and push to
    // the stack.
    emit(get_opcode);
    emit(static_cast<uint8_t>(index));
  }
}

void Parser::named_variable(std::string_view lexeme, bool can_assign) {
  std::optional<size_t> opt_local_index = compiler->resolve_local(lexeme);
  if (opt_local_index.has_value()) {
    size_t local_index = opt_local_index.value();
    emit_variable_access(lox::OpCode::SET_LOCAL, lox::OpCode::GET_LOCAL,
                         can_assign, local_index);
  } else {
    // Check if it's an upvalue.
    std::optional<size_t> opt_upvalue_index = compiler->resolve_upvalue(lexeme);
    if (opt_upvalue_index.has_value()) {
      size_t upvalue_index = opt_upvalue_index.value();
      emit_variable_access(lox::OpCode::SET_UPVALUE, lox::OpCode::GET_UPVALUE,
                           can_assign, upvalue_index);
    } else {
      // Not found, so it's a global variable (it might be undefined, but
      // that's a runtime error). We can't error in the compiler because it
      // might be defined later after we're done compiling the current
      // function.
      ObjString* var_name_str = gc.get_string_ptr(lexeme);
      size_t constant_index = make_constant(var_name_str);
      emit_variable_access(lox::OpCode::SET_GLOBAL, lox::OpCode::GET_GLOBAL,
                           can_assign, constant_index);
    }
  }
  return;
}

void Parser::block() {
  while (!consume_if(TokenType::RIGHT_BRACE) && !is_at_end() && !has_error()) {
    declaration();
  }
}

void Parser::end_scope() {
  std::vector<bool> pops = compiler->end_scope();
  for (bool is_captured : pops) {
    if (is_captured) {
      emit(lox::OpCode::CLOSE_UPVALUE);
    } else {
      emit(lox::OpCode::POP);
    }
  }
}

void Parser::statement() {
  if (consume_if(TokenType::PRINT)) {
    print_statement();
  } else if (consume_if(TokenType::IF)) {
    if_statement();
  } else if (consume_if(TokenType::WHILE)) {
    while_statement();
  } else if (consume_if(TokenType::FOR)) {
    for_statement();
  } else if (consume_if(TokenType::RETURN)) {
    return_statement();
  } else if (consume_if(TokenType::LEFT_BRACE)) {
    compiler->begin_scope();
    block();
    end_scope();
  } else {
    expression_statement();
  }
}

// This function is hit whenever we have a plain `return;` (inside
// `return_statement`) or if we fall off the end of a function without an
// explicit return (inside `finalise_function`). It pushes the implicit return
// value onto the stack (but does not emit the RETURN instruction).
void Parser::emit_auto_return_value() {
  if (compiler->get_function_type() == FunctionType::CLASSINIT) {
    // return the instance, which is happily always at local slot 0.
    emit(lox::OpCode::GET_LOCAL);
    emit(static_cast<uint8_t>(0));
  } else {
    // No return value; return nil
    emit_constant(std::monostate());
  }
}

void Parser::return_statement() {
  if (compiler->get_function_type() == FunctionType::TOPLEVEL) {
    error("cannot return from top-level code", previous.line);
  }
  // Return value
  if (consume_if(TokenType::SEMICOLON)) {
    // No explicit return value.
    emit_auto_return_value();
  } else {
    // Explicit return value.
    if (compiler->get_function_type() == FunctionType::CLASSINIT) {
      error("cannot return a value from an initializer", previous.line);
    }
    expression();
    consume_or_error(TokenType::SEMICOLON, "expected ';' after return value");
  }
  emit(lox::OpCode::RETURN);
}

void Parser::print_statement() {
  expression();
  consume_or_error(TokenType::SEMICOLON,
                   "expected ';' after value in print statement");
  emit(lox::OpCode::PRINT);
}

void Parser::if_statement() {
  consume_or_error(TokenType::LEFT_PAREN, "expected '(' after 'if'");
  expression();
  consume_or_error(TokenType::RIGHT_PAREN, "expected ')' after condition");
  size_t jump_byte = emit_jump(lox::OpCode::JUMP_IF_FALSE);
  // If the condition is true, we will fall through to here. First pop the
  // value, then execute the 'if' branch.
  emit(lox::OpCode::POP);
  statement();
  // When falling through the 'if' branch, we need to jump over the else
  // branch, so we emit an unconditional jump here.
  size_t else_jump_byte = emit_jump(lox::OpCode::JUMP);
  patch_jump(jump_byte, get_chunk_size());
  // Pop the condition value before we enter the else branch.
  emit(lox::OpCode::POP);
  if (consume_if(TokenType::ELSE)) {
    statement();
  }
  patch_jump(else_jump_byte, get_chunk_size());
}

void Parser::while_statement() {
  consume_or_error(TokenType::LEFT_PAREN, "expected '(' after 'while'");
  size_t loop_start = get_chunk_size();
  expression();
  consume_or_error(TokenType::RIGHT_PAREN,
                   "expected ')' after while condition");
  size_t exit_jump = emit_jump(lox::OpCode::JUMP_IF_FALSE);
  // Pop the condition value before entering the loop body.
  emit(lox::OpCode::POP);
  statement();
  size_t loop_jump = emit_jump(lox::OpCode::JUMP);
  patch_jump(loop_jump, loop_start);
  patch_jump(exit_jump, get_chunk_size());
  // Pop the condition value when exiting the loop.
  emit(lox::OpCode::POP);
}

void Parser::for_statement() {
  consume_or_error(TokenType::LEFT_PAREN, "expected '(' after 'for'");
  compiler->begin_scope();
  // Initialiser. Note that all three branches will eat the semicolon at the
  // end.
  if (consume_if(TokenType::SEMICOLON)) {
    // No initialiser
  } else if (consume_if(TokenType::VAR)) {
    var_declaration();
  } else {
    expression_statement();
  }
  // Condition
  size_t cond_start = get_chunk_size();
  bool has_condition = !consume_if(TokenType::SEMICOLON);

  size_t exit_jump = 0;
  if (has_condition) {
    expression();
    consume_or_error(TokenType::SEMICOLON, "expected ';' after loop condition");
    exit_jump = emit_jump(lox::OpCode::JUMP_IF_FALSE);
  }

  size_t to_body_jump = emit_jump(lox::OpCode::JUMP);
  // Increment
  size_t increment_start = get_chunk_size();
  if (consume_if(TokenType::RIGHT_PAREN)) {
    // No increment
  } else {
    expression();
    consume_or_error(TokenType::RIGHT_PAREN, "expected ')' after for clauses");
    emit(lox::OpCode::POP);
  }

  if (has_condition) {
    size_t to_cond_jump = emit_jump(lox::OpCode::JUMP);
    patch_jump(to_cond_jump, cond_start);
  }

  patch_jump(to_body_jump, get_chunk_size());
  if (has_condition) {
    // Pop the condition value before entering the loop body.
    emit(lox::OpCode::POP);
  }
  statement();
  size_t to_increment_jump = emit_jump(lox::OpCode::JUMP);
  patch_jump(to_increment_jump, increment_start);
  if (has_condition) {
    patch_jump(exit_jump, get_chunk_size());
  }
  emit(lox::OpCode::POP);

  end_scope();
}

void Parser::expression_statement() {
  expression();
  consume_or_error(TokenType::SEMICOLON, "expected ';' after expression");
  // Discard the value left on the stack after evaluating the expression.
  emit(lox::OpCode::POP);
}

void Parser::expression() { parse_precedence(Precedence::ASSIGNMENT); }

void Parser::number(bool _) {
  double value = std::stod(std::string(previous.lexeme));
  emit_constant(value);
}

void Parser::this_(bool _) {
  // Note that we can't just check if the current function is CLASSMETHOD ||
  // CLASSINIT, because we might be e.g. inside a nested function inside a
  // class method. That's why we have a separate bit of info in the Parser that
  // tracks whether we're currently allowed to use `this` or not.
  if (!is_in_class()) {
    error("cannot use 'this' outside of a class", previous.line);
    return;
  }
  // We can't assign to this, hence the false. But apart from that, we just
  // treat 'this' like a local variable that happens to be at index 0 (which the
  // constructor of Compiler does for us).
  variable(false);
}

void Parser::literal(bool _) {
  switch (previous.type) {
  case TokenType::FALSE:
    emit_constant(false);
    break;
  case TokenType::NIL:
    emit_constant(std::monostate());
    break;
  case TokenType::TRUE:
    emit_constant(true);
    break;
  default:
    throw std::runtime_error("unreachable: unknown literal type " +
                             to_string(previous.type));
  }
}

void Parser::string(bool _) {
  ObjString* obj_str = gc.get_string_ptr(previous.lexeme);
  emit_constant(obj_str);
}

void Parser::grouping(bool _) {
  expression();
  consume_or_error(TokenType::RIGHT_PAREN, "expected ')'");
}

void Parser::unary(bool _) {
  // We've already consumed the operator.
  TokenType prev_type = previous.type;
  // Operand.
  parse_precedence(Precedence::UNARY);
  // Figure out what to do with the operand.
  switch (prev_type) {
  case TokenType::MINUS:
    emit(lox::OpCode::NEGATE);
    break;
  case TokenType::BANG:
    emit(lox::OpCode::NOT);
    break;
  default:
    throw std::runtime_error("loxc: unknown unary operator " +
                             to_string(prev_type));
  }
}

void Parser::and_operator(bool _) {
  // We've already consumed the 'and' operator, as well as the left operand.
  // If the left operand is truthy, we can get rid of it and then evaluate the
  // right-operand. If it's falsy, we can short-circuit and skip the entire
  // right-operand.
  size_t end_jump = emit_jump(lox::OpCode::JUMP_IF_FALSE);
  // If we don't follow the jump, that means it was truthy. Get rid of it
  // and evaluate the right operand.
  emit(lox::OpCode::POP);
  parse_precedence(Precedence::AND);
  // If we do follow the jump, we can skip over all of that.
  patch_jump(end_jump, get_chunk_size());
}

void Parser::or_operator(bool _) {
  // We've already consumed the 'and' operator, as well as the left operand.
  // This is a bit more complicated because we don't have a JUMP_IF_TRUE
  // opcode.
  size_t jump_to_right_operand = emit_jump(lox::OpCode::JUMP_IF_FALSE);
  // If we don't follow the jump, that means the left operand was truthy, so
  // we can skip the right operand entirely.
  size_t jump_to_end = emit_jump(lox::OpCode::JUMP);
  // Patch the first jump to here, which is the start of the right operand.
  patch_jump(jump_to_right_operand, get_chunk_size());
  emit(lox::OpCode::POP);
  parse_precedence(Precedence::OR);
  // Patch the jump to the end to here, which is after the right operand.
  patch_jump(jump_to_end, get_chunk_size());
}

void Parser::dot(bool can_assign) {
  // dot has already been consumed; left operand already pushed to stack

  // parse 'right operand' which is the field name
  consume_or_error(TokenType::IDENTIFIER, "expected property name after '.'");
  std::string_view field_name = previous.lexeme;
  // push it to the constant table
  uint8_t name_constant_index = make_constant(gc.get_string_ptr(field_name));

  emit_variable_access(lox::OpCode::SET_PROPERTY, lox::OpCode::GET_PROPERTY,
                       can_assign, name_constant_index);
}

void Parser::binary(bool _) {
  TokenType prev_type = previous.type;
  // Get the precedence of the operator we just consumed.
  Rule rule = get_rule(prev_type);
  // Parse the right operand. The right operand might *itself* be a complex
  // expression -- for example, we might be parsing the "2 + 3" bit of (1 * 2 +
  // 3). However, we need to make sure that when parsing the right operand, we
  // only parse operators that have higher precedence than the current operator.
  //
  // In this example, rule.precedence is the precedence of '*', and the
  // precedence of '+' is lower than that. That means that when we call
  // parse_precedence with next_precedence(rule.precedence), we WON'T parse the
  // '+' operator; we'll only parse the "2".
  //
  // After that, when we return to the previous call to parse_precedence, we'll
  // be able to parse the '+' operator. That allows us to correctly parse the
  // expression as (1 * 2) + 3 rather than 1 * (2 + 3).
  parse_precedence(next_precedence(rule.precedence));
  // Emit the operator instruction.
  switch (prev_type) {
  case TokenType::PLUS:
    emit(lox::OpCode::ADD);
    break;
  case TokenType::MINUS:
    emit(lox::OpCode::SUBTRACT);
    break;
  case TokenType::STAR:
    emit(lox::OpCode::MULTIPLY);
    break;
  case TokenType::SLASH:
    emit(lox::OpCode::DIVIDE);
    break;
  case TokenType::EQUAL_EQUAL:
    emit(lox::OpCode::EQUAL);
    break;
  case TokenType::BANG_EQUAL:
    emit(lox::OpCode::EQUAL);
    emit(lox::OpCode::NOT);
    break;
  case TokenType::GREATER:
    emit(lox::OpCode::GREATER);
    break;
  case TokenType::GREATER_EQUAL:
    emit(lox::OpCode::LESS);
    emit(lox::OpCode::NOT);
    break;
  case TokenType::LESS:
    emit(lox::OpCode::LESS);
    break;
  case TokenType::LESS_EQUAL:
    emit(lox::OpCode::GREATER);
    emit(lox::OpCode::NOT);
    break;
  default:
    throw std::runtime_error("unreachable: unknown binary operator " +
                             to_string(prev_type));
  }
}

} // namespace lox
