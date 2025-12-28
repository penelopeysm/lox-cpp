#include "compiler.hpp"
#include "chunk.hpp"
#include "scanner.hpp"
#include <iostream>
#include <string_view>

namespace {
lox::scanner::Token SENTINEL_EOF =
    lox::scanner::Token(lox::scanner::TokenType::_EOF, "", 0);
}

namespace lox {

using lox::scanner::Scanner;

Parser::Parser(std::unique_ptr<Scanner> scanner)
    : scanner(std::move(scanner)), current(SENTINEL_EOF),
      previous(SENTINEL_EOF), had_error(false) {}

void Parser::advance() {
  previous = current;
  while (true) {
    current = scanner->scan_token();
    // std::cout << to_string(current) << "\n";
    if (current.type != scanner::TokenType::ERROR)
      break;
    // Handle error token (e.g., report error)
    had_error = true;
    std::cerr << "Error at line " << current.line << ": " << to_string(current)
              << "\n";
  }
}

bool compile(std::string_view source, Chunk& chunk) {
  // Instantiates a scanner that holds the source code; but doesn't actually
  // perform any scanning. Scanning will happen on demand!
  std::unique_ptr<Scanner> scanner =
      std::make_unique<Scanner>(source);
  // Scanner scanner(source);
  // Then instantiate a parser that holds the scanner.
  Parser parser(std::move(scanner));

  // TODO: actually do this correctly
  while (!parser.is_at_end()) {
    parser.advance();
  }
  chunk.write(lox::OpCode::CONSTANT, 42);
  chunk.push_constant(2.0);
  chunk.write(0, 42); // index of constant 2.0
  chunk.write(OpCode::RETURN, 42);

  return true;
}

} // namespace lox
