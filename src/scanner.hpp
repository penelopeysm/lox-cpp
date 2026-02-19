#pragma once
#include <cstddef>
#include <string_view>
#include <string>
#include <functional>

namespace lox::scanner {

enum class TokenType {
  // Single-character tokens.
  LEFT_PAREN,
  RIGHT_PAREN,
  LEFT_BRACE,
  RIGHT_BRACE,
  COMMA,
  DOT,
  MINUS,
  PLUS,
  SEMICOLON,
  SLASH,
  STAR,
  // One or two character tokens.
  BANG,
  BANG_EQUAL,
  EQUAL,
  EQUAL_EQUAL,
  GREATER,
  GREATER_EQUAL,
  LESS,
  LESS_EQUAL,
  // Literals.
  IDENTIFIER,
  STRING,
  NUMBER,
  // Keywords.
  AND,
  CLASS,
  ELSE,
  FALSE,
  FOR,
  FUN,
  IF,
  NIL,
  OR,
  PRINT,
  RETURN,
  SUPER,
  THIS,
  TRUE,
  VAR,
  WHILE,
  // Miscellaneous
  ERROR,
  _EOF,
};
std::string to_string(TokenType type);

class Token {
public:
  TokenType type;
  std::string_view lexeme;
  std::size_t line;

  Token(TokenType type, std::string_view lexeme, std::size_t line)
      : type(type), lexeme(lexeme), line(line) {}
};
std::string to_string(const Token& token);

class Scanner {
public:
  explicit Scanner(std::string_view source);
  void scan_and_print();
  Token scan_token();
  bool is_at_end() const { return current == source.end(); }

private:
  std::string_view source;
  std::size_t line;

  // NOTE: const_iterator is an iterator that points to a const value (not a
  // pointer that is itself const). That makes sense because string_view is not
  // mutable (or cannot mutate the underlying string).

  // Start of current lexeme
  std::string_view::const_iterator start;
  // Current character being examined
  std::string_view::const_iterator current;

  Token make_token(TokenType type);
  Token make_token(TokenType type, size_t skip_begin, size_t skip_end);
  Token make_error_token(std::string_view message);
  char peek() const { return *current; }
  char consume() { return *current++; }
  char rewind() { return *--current; }
  bool consume_next_if(char expected) {
    if (is_at_end() || *current != expected) {
      return false;
    }
    if (expected == '\n') {
      line++;
    }
    current++;
    return true;
  }
  void consume_while(std::function<bool(char)> predicate) {
    while (!is_at_end() && predicate(*current)) {
      if (*current == '\n') {
        line++;
      }
      current++;
    }
  }
};

} // namespace lox::scanner
