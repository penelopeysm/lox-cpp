#include "compiler.hpp"
#include "chunk.hpp"
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
}

namespace lox {

Precedence next_precedence(Precedence in) {
  if (in == Precedence::PRIMARY) {
    throw std::runtime_error("unreachable: PRIMARY has the highest precedence");
  }
  return static_cast<Precedence>(static_cast<int>(in) + 1);
}

size_t Compiler::end_scope() {
  size_t num_popped = 0;
  while (!locals.empty() && locals.back().depth == scope_depth) {
    locals.pop_back();
    ++num_popped;
  }
  --scope_depth;
  return num_popped;
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
  locals.push_back(Local{scope_depth, name});
  return false;
}

int Compiler::resolve_local(std::string_view name) {
  for (int i = static_cast<int>(locals.size()) - 1; i >= 0; --i) {
    if (locals[i].name == name) {
      return i;
    }
  }
  return -1;
}

using lox::scanner::Scanner;
using lox::scanner::TokenType;

Parser::Parser(std::unique_ptr<Scanner> scanner, Chunk chunk,
               StringMap& string_map)
    : scanner(std::move(scanner)), current(SENTINEL_EOF),
      previous(SENTINEL_EOF), errmsg(std::nullopt), string_map(string_map),
      chunk(std::move(chunk)), compiler() {}

void Parser::advance() {
  previous = current;
  current = scanner->scan_token();
  // std::cout << to_string(current) << "\n";
  if (current.type == TokenType::ERROR) {
    error(current.lexeme, current.line);
  }
}

// This will consume `parser.current` if it matches `type` (and that means that
// the consumed token will be stored in `parser.previous`!).
bool Parser::consume_or_error(TokenType type, std::string_view error_message) {
  if (current.type == type) {
    advance();
    return true;
  } else {
    error(error_message, current.line);
    return false;
  }
}

bool Parser::consume_if(TokenType type) {
  if (current.type == type) {
    advance();
    return true;
  } else {
    return false;
  }
}

void Parser::error(std::string_view message, size_t line) {
  errmsg = std::pair(std::string(message), line);
}

void Parser::report_error() {
  if (errmsg.has_value()) {
    std::cerr << "[line " << errmsg->second << "] Error: " << errmsg->first
              << "\n";
  } else {
    throw std::runtime_error("unreachable: no error to report");
  }
}

size_t Parser::make_constant(lox::Value value) {
  size_t constant_index = chunk.push_constant(value);
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
    next_rule = get_rule(current.type);
  }
}

void Parser::declaration() {
  if (consume_if(TokenType::VAR)) {
    var_declaration();
  } else {
    statement();
  }
}

void Parser::var_declaration() {
  // Parse identifier
  consume_or_error(TokenType::IDENTIFIER, "expected variable name");
  std::string_view var_name = previous.lexeme;
  // Determine whether it's a global or local variable
  bool is_local = compiler.get_scope_depth() > 0;
  // Initializer. These instructions, when executed, will add the initial value
  // to the top of the stack.
  if (consume_if(TokenType::EQUAL)) {
    expression();
  } else {
    // if no initializer provided, initialize to nil
    emit_constant(std::monostate());
  }
  // For a local variable, we just need to define it in the compiler.
  if (is_local) {
    compiler.declare_local(var_name);
  } else {
    define_global_variable(var_name);
  }
  consume_or_error(TokenType::SEMICOLON,
                   "expected ';' after variable declaration");
}

void Parser::define_global_variable(std::string_view name) {
  std::shared_ptr<ObjString> var_name_str = string_map.get_ptr(name);
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

void Parser::named_variable(std::string_view lexeme, bool can_assign) {
  int local_index = compiler.resolve_local(lexeme);
  if (local_index == -1) {
    // Not found, so it's a global variable
    std::shared_ptr<ObjString> var_name_str = string_map.get_ptr(lexeme);
    size_t constant_index = make_constant(var_name_str);
    // It might be an assignment though...
    if (consume_if(TokenType::EQUAL)) {
      if (!can_assign) {
        error("invalid assignment target", previous.line);
      }
      // It's an assignment; evaluate the RHS expression first.
      expression();
      emit(lox::OpCode::SET_GLOBAL);
      emit(static_cast<uint8_t>(constant_index));
    } else {
      // Just variable access.
      emit(lox::OpCode::GET_GLOBAL);
      emit(static_cast<uint8_t>(constant_index));
    }
  } else {
    // It's a local variable located at local_index on the stack.
    if (consume_if(TokenType::EQUAL)) {
      if (!can_assign) {
        error("invalid assignment target", previous.line);
      }
      expression();
      emit(lox::OpCode::SET_LOCAL);
      emit(static_cast<uint8_t>(local_index));
    } else {
      emit(lox::OpCode::GET_LOCAL);
      emit(static_cast<uint8_t>(local_index));
    }
  }
  return;
}

void Parser::block() {
  while (!consume_if(TokenType::RIGHT_BRACE) && !is_at_end() && !has_error()) {
    declaration();
  }
}

void Parser::statement() {
  if (consume_if(TokenType::PRINT)) {
    print_statement();
  } else if (consume_if(TokenType::LEFT_BRACE)) {
    compiler.begin_scope();
    block();
    size_t num_popped = compiler.end_scope();
    for (size_t i = 0; i < num_popped; ++i) {
      emit(lox::OpCode::POP);
    }
  } else {
    expression_statement();
  }
}

void Parser::print_statement() {
  expression();
  consume_or_error(TokenType::SEMICOLON, "expected ';' after value");
  emit(lox::OpCode::PRINT);
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
  std::shared_ptr<ObjString> obj_str = string_map.get_ptr(previous.lexeme);
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

void Parser::binary(bool _) {
  TokenType prev_type = previous.type;
  // Get the precedence of the operator we just consumed.
  Rule rule = get_rule(prev_type);
  // Parse the right operand.
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
