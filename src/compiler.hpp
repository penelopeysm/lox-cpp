#include "chunk.hpp"
#include "scanner.hpp"
#include <memory>
#include <string_view>

namespace lox {

enum class Precedence {
  NONE,
  ASSIGNMENT, // =
  OR,         // or
  AND,        // and
  EQUALITY,   // == !=
  COMPARISON, // < > <= >=
  TERM,       // + -
  FACTOR,     // * /
  UNARY,      // ! -
  CALL,       // . ()
  PRIMARY
};
Precedence next_precedence(Precedence in);

class Parser {
public:
  Parser(std::unique_ptr<scanner::Scanner> scanner, Chunk chunk);
  void parse();
  Chunk get_chunk() const { return chunk; }
  bool error_occurred() const { return errmsg.has_value(); }
  void report_error();

private:
  std::unique_ptr<scanner::Scanner> scanner;
  scanner::Token current;
  scanner::Token previous;
  std::optional<std::pair<std::string, size_t>> errmsg;
  Chunk chunk;

  // Interact with scanner
  void advance();
  bool consume_or_error(scanner::TokenType type,
                        std::string_view error_message);
  bool is_at_end() const { return scanner->is_at_end(); }
  void error(std::string_view message, size_t line);

  // Parsing methods
  void parse_precedence(Precedence precedence);
  void expression();
  void number();
  void grouping();
  void unary();
  void binary();
  void string();
  // Not technically all literals... this handles true, false, and nil.
  void literal();

  // Interact with chunk
  void emit(uint8_t byte) { chunk.write(byte, previous.line); }
  void emit(lox::OpCode opcode) { emit(static_cast<uint8_t>(opcode)); }
  void emit_constant(lox::Value value);

  using ParserMemFn = void (Parser::*)();
  struct Rule {
    ParserMemFn prefix;
    ParserMemFn infix;
    Precedence precedence;
  };

  constexpr Rule get_rule(scanner::TokenType t) {
    using scanner::TokenType;
    switch (t) {
    case TokenType::LEFT_PAREN:
      return Rule{&Parser::grouping, nullptr, Precedence::NONE};
    case TokenType::RIGHT_PAREN:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::LEFT_BRACE:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::RIGHT_BRACE:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::COMMA:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::DOT:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::MINUS:
      return Rule{&Parser::unary, &Parser::binary, Precedence::TERM};
    case TokenType::PLUS:
      return Rule{NULL, &Parser::binary, Precedence::TERM};
    case TokenType::SEMICOLON:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::SLASH:
      return Rule{NULL, &Parser::binary, Precedence::FACTOR};
    case TokenType::STAR:
      return Rule{NULL, &Parser::binary, Precedence::FACTOR};
    case TokenType::BANG:
      return Rule{&Parser::unary, NULL, Precedence::NONE};
    case TokenType::BANG_EQUAL:
      return Rule{NULL, &Parser::binary, Precedence::EQUALITY};
    case TokenType::EQUAL:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::EQUAL_EQUAL:
      return Rule{NULL, &Parser::binary, Precedence::EQUALITY};
    case TokenType::GREATER:
      return Rule{NULL, &Parser::binary, Precedence::COMPARISON};
    case TokenType::GREATER_EQUAL:
      return Rule{NULL, &Parser::binary, Precedence::COMPARISON};
    case TokenType::LESS:
      return Rule{NULL, &Parser::binary, Precedence::COMPARISON};
    case TokenType::LESS_EQUAL:
      return Rule{NULL, &Parser::binary, Precedence::COMPARISON};
    case TokenType::IDENTIFIER:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::STRING:
      return Rule{&Parser::string, NULL, Precedence::NONE};
    case TokenType::NUMBER:
      return Rule{&Parser::number, NULL, Precedence::NONE};
    case TokenType::AND:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::CLASS:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::ELSE:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::FALSE:
      return Rule{&Parser::literal, NULL, Precedence::NONE};
    case TokenType::FOR:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::FUN:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::IF:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::NIL:
      return Rule{&Parser::literal, NULL, Precedence::NONE};
    case TokenType::OR:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::PRINT:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::RETURN:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::SUPER:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::THIS:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::TRUE:
      return Rule{&Parser::literal, NULL, Precedence::NONE};
    case TokenType::VAR:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::WHILE:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::ERROR:
      return Rule{NULL, NULL, Precedence::NONE};
    case TokenType::_EOF:
      return Rule{NULL, NULL, Precedence::NONE};
    default:
      throw std::runtime_error("No rule for this token type");
    }
  }
};

} // namespace lox
