#include "chunk.hpp"

Chunk::Chunk() : code() {}

void Chunk::display() const {
  printf("chunk with %zu bytes\n", code.size());
  for (auto byte: code) {
    printf("%02x ", byte);
  }
}
