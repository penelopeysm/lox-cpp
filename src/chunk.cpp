#include "chunk.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>

Chunk::Chunk() : code() {}

void Chunk::display() const {
  std::cout << " --- Chunk with " << code.size() << " bytes --- " << std::endl;
  for (size_t i = 0; i < code.size(); ++i) {
    uint8_t byte = code[i];
    if (i % 16 == 0) {
      std::cout << std::setw(4) << std::setfill('0') << i << ": ";
    }
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(byte) << " ";
    if (i % 16 == 15 || i == code.size() - 1) {
      std::cout << std::endl;
    }
  }
  std::cout << std::dec;
}

Chunk &Chunk::write(uint8_t byte) {
  try {
    code.push_back(byte);
  } catch (const std::bad_alloc &) {
    // Gracefully handle OOM
    std::cerr << "loxc: out of memory while writing to Chunk" << std::endl;
    std::exit(EXIT_FAILURE);
  }
  return *this;
}
