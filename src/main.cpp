#include "chunk.hpp"
#include <iostream>

int main() {
  std::cout << "Hello, world." << std::endl;
  lox::Chunk chunk;

  const size_t line = 1;
  chunk.write(lox::OpCode::RETURN, line);
  chunk.push_constant(42.0);
  chunk.write(lox::OpCode::CONSTANT, line);
  chunk.write(0, line); // index of constant 42.0

  std::cout << "\n\n" << chunk << "\n\n";
  return 0;
}
