#include "scanner.hpp"
#include <ostream>
#include <cctype>
#include <format>
#include <sstream>
#include <string>

// This is temporary, remove this when we don't need to debug print stuff
#include <iostream>

namespace lox::scanner {

std::string to_string(const TokenType type) {
  switch (type) {
  case TokenType::LEFT_PAREN:
    return "LEFT_PAREN";
  case TokenType::RIGHT_PAREN:
    return "RIGHT_PAREN";
  case TokenType::LEFT_BRACE:
    return "LEFT_BRACE";
  case TokenType::RIGHT_BRACE:
    return "RIGHT_BRACE";
  case TokenType::COMMA:
    return "COMMA";
  case TokenType::DOT:
    return "DOT";
  case TokenType::MINUS:
    return "MINUS";
  case TokenType::PLUS:
    return "PLUS";
  case TokenType::SEMICOLON:
    return "SEMICOLON";
  case TokenType::SLASH:
    return "SLASH";
  case TokenType::STAR:
    return "STAR";
  case TokenType::BANG:
    return "BANG";
  case TokenType::BANG_EQUAL:
    return "BANG_EQUAL";
  case TokenType::EQUAL:
    return "EQUAL";
  case TokenType::EQUAL_EQUAL:
    return "EQUAL_EQUAL";
  case TokenType::GREATER:
    return "GREATER";
  case TokenType::GREATER_EQUAL:
    return "GREATER_EQUAL";
  case TokenType::LESS:
    return "LESS";
  case TokenType::LESS_EQUAL:
    return "LESS_EQUAL";
  case TokenType::IDENTIFIER:
    return "IDENTIFIER";
  case TokenType::STRING:
    return "STRING";
  case TokenType::NUMBER:
    return "NUMBER";
  case TokenType::AND:
    return "AND";
  case TokenType::CLASS:
    return "CLASS";
  case TokenType::ELSE:
    return "ELSE";
  case TokenType::FALSE:
    return "FALSE";
  case TokenType::FOR:
    return "FOR";
  case TokenType::FUN:
    return "FUN";
  case TokenType::IF:
    return "IF";
  case TokenType::NIL:
    return "NIL";
  case TokenType::OR:
    return "OR";
  case TokenType::PRINT:
    return "PRINT";
  case TokenType::RETURN:
    return "RETURN";
  case TokenType::SUPER:
    return "SUPER";
  case TokenType::THIS:
    return "THIS";
  case TokenType::TRUE:
    return "TRUE";
  case TokenType::VAR:
    return "VAR";
  case TokenType::WHILE:
    return "WHILE";
  case TokenType::ERROR:
    return "ERROR";
  case TokenType::_EOF:
    return "_EOF";
  }
}

std::string to_string(const Token& token) {
  std::ostringstream ss;
  ss << "Token(" << to_string(token.type) << ", \"" << std::string(token.lexeme)
     << "\", " << token.line << ")";
  return ss.str();
}

std::ostream& operator<<(std::ostream& os, const Token& token) {
  return os << to_string(token);
}

Scanner::Scanner(std::string_view source)
    : source(source), line(1), start(source.begin()), current(source.begin()) {}

void Scanner::scan_and_print() {
  // Mainly for debugging.
  size_t previous_line =
      0; // Initialise to 0 to make sure first line is printed
  while (true) {
    Token token = scan_token();
    if (line != previous_line) {
      previous_line = line;
      std::cerr << std::format("{:4} ", line);
    } else {
      std::cerr << "   | ";
    }
    std::cerr << token << "\n";

    if (token.type == TokenType::_EOF)
      break;
  }
}

Token Scanner::make_token(TokenType t) {
  return Token(t, std::string_view(start, current - start), line);
}
Token Scanner::make_token(TokenType t, size_t skip_begin, size_t skip_end) {
  return Token(t,
               std::string_view(start + skip_begin,
                                current - start - skip_end - skip_begin),
               line);
}

Token Scanner::make_error_token(std::string_view message) {
  return Token(TokenType::ERROR, message, line);
}

Token Scanner::scan_token() {
  start = current;
  if (is_at_end()) {
    return make_token(TokenType::_EOF);
  }

  char c = consume();
  switch (c) {
  case '(':
    return make_token(TokenType::LEFT_PAREN);
  case ')':
    return make_token(TokenType::RIGHT_PAREN);
  case '{':
    return make_token(TokenType::LEFT_BRACE);
  case '}':
    return make_token(TokenType::RIGHT_BRACE);
  case ';':
    return make_token(TokenType::SEMICOLON);
  case ',':
    return make_token(TokenType::COMMA);
  case '.':
    return make_token(TokenType::DOT);
  case '-':
    return make_token(TokenType::MINUS);
  case '+':
    return make_token(TokenType::PLUS);
  case '/':
    if (consume_next_if('/')) {
      consume_while([](char ch) { return ch != '\n'; });
      // We can't just do `break`, because that would fall through to the
      // default case.
      return scan_token();
    } else {
      return make_token(TokenType::SLASH);
    }
  case '*':
    return make_token(TokenType::STAR);
  case '!':
    return make_token(consume_next_if('=') ? TokenType::BANG_EQUAL
                                           : TokenType::BANG);
  case '=':
    return make_token(consume_next_if('=') ? TokenType::EQUAL_EQUAL
                                           : TokenType::EQUAL);
  case '<':
    return make_token(consume_next_if('=') ? TokenType::LESS_EQUAL
                                           : TokenType::LESS);
  case '>':
    return make_token(consume_next_if('=') ? TokenType::GREATER_EQUAL
                                           : TokenType::GREATER);
  // Whitespace
  case ' ':
  case '\r':
  case '\t':
    // Ignore whitespace and scan the next token.
    return scan_token();
  case '\n':
    line++;
    return scan_token();

  // Strings.
  case '"':
    consume_while([](char ch) { return ch != '"'; });
    consume(); // Closing quote
    if (is_at_end()) {
      return make_error_token("unterminated string literal");
    } else {
      return make_token(TokenType::STRING, 1, 1);
    }
  }

  // Numbers
  if (std::isdigit(c)) {
    consume_while([](char ch) { return std::isdigit(ch); });
    // Look for a fractional part.
    if (consume_next_if('.')) {
      if (std::isdigit(peek())) {
        consume_while([](char ch) { return std::isdigit(ch); });
      } else {
        // rewind the dot
        rewind();
      }
    }
    return make_token(TokenType::NUMBER);
  }

  // Identifiers. Honestly, I'm lazy to use the trie structure the book uses.
  // Sorry. Better things to spend my time on.
  if (std::isalpha(c) || c == '_') {
    consume_while([](char ch) { return std::isalnum(ch) || ch == '_'; });
    std::string_view text(start, current - start);
    if (text == "and")
      return make_token(TokenType::AND);
    if (text == "class")
      return make_token(TokenType::CLASS);
    if (text == "else")
      return make_token(TokenType::ELSE);
    if (text == "false")
      return make_token(TokenType::FALSE);
    if (text == "for")
      return make_token(TokenType::FOR);
    if (text == "fun")
      return make_token(TokenType::FUN);
    if (text == "if")
      return make_token(TokenType::IF);
    if (text == "nil")
      return make_token(TokenType::NIL);
    if (text == "or")
      return make_token(TokenType::OR);
    if (text == "print")
      return make_token(TokenType::PRINT);
    if (text == "return")
      return make_token(TokenType::RETURN);
    if (text == "super")
      return make_token(TokenType::SUPER);
    if (text == "this")
      return make_token(TokenType::THIS);
    if (text == "true")
      return make_token(TokenType::TRUE);
    if (text == "var")
      return make_token(TokenType::VAR);
    if (text == "while")
      return make_token(TokenType::WHILE);
    return make_token(TokenType::IDENTIFIER);
  }

  return make_error_token("unrecognized character");
}

} // namespace lox::scanner
