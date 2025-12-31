#include "vm.hpp"
#include <fstream>
#include <iostream>
#include <string>

void runRepl() {
  std::string line;
  while (true) {
    std::cout << "> ";
    if (!std::getline(std::cin, line)) {
      break;
    }
    lox::InterpretResult result = lox::interpret(line);
    if (result == lox::InterpretResult::COMPILE_ERROR) {
      continue;
    }
  }
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
