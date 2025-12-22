#include "chunk.hpp"
#include "vm.hpp"

int main() {
  lox::Chunk chunk;
  const size_t line = 1;
  chunk.write(lox::OpCode::CONSTANT, line);
  chunk.push_constant(42.0);
  chunk.write(0, line); // index of constant 42.0
  chunk.write(lox::OpCode::RETURN, line);

  // std::cout << "\n\n" << chunk << "\n\n";

  lox::VM vm(chunk);
  vm.run();
  return 0;
}
