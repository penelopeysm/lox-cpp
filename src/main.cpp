#include "chunk.hpp"
#include <iostream>

int main() {
  std::cout << "Hello, world." << std::endl;
  lox::Chunk chunk;
  chunk.write(lox::OpCode::RETURN);
  std::cout << chunk;
  return 0;
}
