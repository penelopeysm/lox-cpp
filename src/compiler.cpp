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

using lox::scanner::Scanner;
using lox::scanner::TokenType;

Parser::Parser(std::unique_ptr<Scanner> scanner, Chunk chunk,
               StringMap& string_map)
    : scanner(std::move(scanner)), current(SENTINEL_EOF),
      previous(SENTINEL_EOF), errmsg(std::nullopt), string_map(string_map),
      chunk(chunk) {}

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

void Parser::emit_constant(lox::Value value) {
  size_t constant_index = chunk.push_constant(value);
  if (constant_index > UINT8_MAX) {
    error("Too many constants in one chunk.", previous.line);
    return;
  }
  emit(lox::OpCode::CONSTANT);
  emit(static_cast<uint8_t>(constant_index));
}

void Parser::parse() {
  advance(); // Load the first token into `current`.
  expression();
  emit(lox::OpCode::RETURN);
}

void Parser::parse_precedence(Precedence precedence) {
  advance();
  Rule prefix_rule = get_rule(previous.type);
  if (prefix_rule.prefix == nullptr) {
    error("expected expression", previous.line);
    return;
  }
  // Call the prefix parse function.
  // NOTE: std::invoke is a nicer way of calling member function pointers.
  // The alternative is: (this->*prefix_rule.prefix)();
  std::invoke(prefix_rule.prefix, this);

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
      std::invoke(infix_rule, this);
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

void Parser::expression() { parse_precedence(Precedence::ASSIGNMENT); }

void Parser::number() {
  double value = std::stod(std::string(previous.lexeme));
  emit_constant(value);
}

void Parser::literal() {
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

void Parser::string() {
  std::shared_ptr<ObjString> obj_str = string_map.get_ptr(previous.lexeme);
  emit_constant(obj_str);
}

void Parser::grouping() {
  expression();
  consume_or_error(TokenType::RIGHT_PAREN, "expected ')'");
}

void Parser::unary() {
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

void Parser::binary() {
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
