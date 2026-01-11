#include "chunk.hpp"
#include "scanner.hpp"
#include "stringmap.hpp"
#include <memory>
#include <optional>
#include <stdexcept>
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

struct Local {
  size_t depth;
  std::string_view name;
};

class Compiler {
public:
  Compiler(std::unique_ptr<ObjFunction> fnptr, bool is_top_level)
      : scope_depth(0), current_function(std::move(fnptr)), is_top_level(is_top_level) {
    // Reserve slot 0 of the stack for VM internal use. Note that this, on its
    // own, will mess with the VM's state since this does not actually push to
    // the stack at runtime. We have to 'balance' this out in the VM by pushing
    // the pointer to the current function onto the stack before we start
    // executing the chunk.
    declare_local("");
  }
  void begin_scope() { scope_depth++; }
  // Returns the number of locals removed from the locals list when exiting a
  // scope. Parser needs to know this so that it can emit POP instructions.
  [[nodiscard]] size_t end_scope();
  [[nodiscard]] int resolve_local(std::string_view name);
  size_t get_scope_depth() const { return scope_depth; }
  // Returns a boolean indicating whether there was an error declaring the
  // local variable (e.g., duplicate variable name in the same scope).
  bool declare_local(std::string_view name);

  size_t get_chunk_size() const {
    return current_function->chunk.size();
  }
  std::unique_ptr<ObjFunction> extract_current_function() {
    return std::move(current_function);
  }
  void emit(uint8_t byte, size_t line) {
    current_function->chunk.write(byte, line);
  }
  // Returns the index of the constant just added
  size_t push_constant(lox::Value value) {
    return current_function->chunk.push_constant(value);
  }
  void patch_at_offset(size_t offset, uint8_t high_byte, uint8_t low_byte) {
    current_function->chunk.patch_at_offset(offset, high_byte);
    current_function->chunk.patch_at_offset(offset + 1, low_byte);
  }

private:
  std::vector<Local> locals;
  size_t scope_depth;
  std::unique_ptr<ObjFunction> current_function;
  bool is_top_level;
};

class Parser {
public:
  Parser(std::unique_ptr<scanner::Scanner> scanner,
         std::unique_ptr<ObjFunction> fnptr, bool is_top_level,
         StringMap& string_map);
  void parse();
  std::unique_ptr<ObjFunction> finalise_function();

private:
  std::unique_ptr<scanner::Scanner> scanner;
  scanner::Token current;
  scanner::Token previous;
  std::optional<std::pair<std::string, size_t>> errmsg;
  StringMap& string_map;
  Compiler compiler;

  // Interact with scanner
  void advance();
  bool consume_or_error(scanner::TokenType type,
                        std::string_view error_message);
  bool consume_if(scanner::TokenType type);
  // NOTE: the `const` annotation here means that the function does not modify
  // any member variables of the Parser class. That allows us to have
  // compile-time guarantees that calling this function won't change the state
  // of the Parser, e.g. if we write const Parser p = ...; then we can still
  // call p.error_occurred().
  bool is_at_end() const { return scanner->is_at_end(); }
  void error(std::string_view message, size_t line);
  bool has_error() const { return errmsg.has_value(); }

  // Parsing methods
  void parse_precedence(Precedence precedence);
  void block();
  void declaration();
  void var_declaration();
  void statement();
  void print_statement();
  void if_statement();
  void while_statement();
  void for_statement();
  void expression_statement();
  void expression();
  void number(bool can_assign);
  void grouping(bool can_assign);
  void unary(bool can_assign);
  void binary(bool can_assign);
  void and_operator(bool can_assign);
  void or_operator(bool can_assign);
  void string(bool can_assign);
  void variable(bool can_assign);
  void literal(bool can_assign);
  void define_global_variable(std::string_view name);
  void named_variable(std::string_view lexeme, bool can_assign);

  // Interact with chunk
  void emit(uint8_t byte) { compiler.emit(byte, previous.line); }
  void emit(lox::OpCode opcode) { emit(static_cast<uint8_t>(opcode)); }
  size_t get_chunk_size() const { return compiler.get_chunk_size(); }
  // Pushes to the constant table and returns the index of the constant just
  // added.
  size_t make_constant(lox::Value value);
  // Pushes to the constant table and *additionally* emits the CONSTANT
  // instruction. Returns the index of the constant just added.
  size_t emit_constant(lox::Value value);
  size_t emit_jump(lox::OpCode jump_opcode);
  void patch_jump(size_t jump_byte, size_t jump_offset);

  using ParserMemFn = void (Parser::*)(bool can_assign);
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
      return Rule{&Parser::variable, NULL, Precedence::NONE};
    case TokenType::STRING:
      return Rule{&Parser::string, NULL, Precedence::NONE};
    case TokenType::NUMBER:
      return Rule{&Parser::number, NULL, Precedence::NONE};
    case TokenType::AND:
      return Rule{NULL, &Parser::and_operator, Precedence::AND};
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
      return Rule{NULL, &Parser::or_operator, Precedence::OR};
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
