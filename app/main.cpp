#include "chunk.hpp"
#include "vm.hpp"
#include <fstream>
#include <iostream>
#include <string>

void runRepl() {
  lox::Chunk chunk;

  // simulate `return 2 + 3;`
  const size_t line = 1;
  chunk.write(lox::OpCode::CONSTANT, line);
  chunk.push_constant(2.0);
  chunk.write(0, line); // index of constant 2.0
  chunk.write(lox::OpCode::CONSTANT, line);
  chunk.push_constant(3.0);
  chunk.write(1, line); // index of constant 3.0
  chunk.write(lox::OpCode::ADD, line);
  chunk.write(lox::OpCode::RETURN, line);

  // std::cout << "\n\n" << chunk << "\n\n";
  lox::VM vm(chunk);
  vm.run();
  exit(0);
}

void runFile(const char* path) {
  // NOTE: std::ios::ate moves the file pointer to the end upon opening.
  // std::ios::binary is a bit more nuanced: it doesn't make a difference on
  // macOS/Linux, but on Windows if you don't use it, \r\n gets read as one
  // character instead of two. So I guess it's More Portable to use it.
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "Could not open file \"" << path << "\"\n";
    exit(74);
  }
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);
  std::string source(size, '\0');
  // NOTE: source.data() returns the raw char* pointer which is what
  // std::ifstream::read wants.
  if (!file.read(source.data(), size)) {
    std::cerr << "Could not read file \"" << path << "\"\n";
    exit(74);
  }
  lox::InterpretResult result = lox::interpret(source);
  switch (result) {
  case lox::InterpretResult::COMPILE_ERROR:
    exit(65);
  case lox::InterpretResult::RUNTIME_ERROR:
    exit(70);
  default:
    exit(0);
  }
}

int main(int argc, char* argv[]) {
  if (argc == 1)
    runRepl();
  else if (argc == 2)
    runFile(argv[1]);
  else {
    std::cerr << "Usage: " << argv[0] << " [script]" << std::endl;
    return 64;
  }
}
