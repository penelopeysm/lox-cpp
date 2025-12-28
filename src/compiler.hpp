#include "chunk.hpp"
#include "scanner.hpp"
#include <string_view>

namespace lox {

bool compile(std::string_view source, Chunk& chunk);

class Parser {
public:
  Parser(std::unique_ptr<scanner::Scanner> scanner);
  void advance();
  bool is_at_end() const { return scanner->is_at_end(); }

private:
  std::unique_ptr<scanner::Scanner> scanner;
  scanner::Token current;
  scanner::Token previous;
  bool had_error;
};

} // namespace lox
