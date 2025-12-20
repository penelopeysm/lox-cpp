#include "chunk.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>

Chunk::Chunk() : code() {}

std::ostream &operator<<(std::ostream &os, const Chunk &chunk) {
  // NOTE: "\n" vs std::endl: the latter always flushes the stream, which is
  // not always what we want (e.g. if writing to a file, it may be more
  // efficient to buffer).
  os << " --- Chunk with " << chunk.code.size() << " bytes --- " << "\n";
  for (size_t i = 0; i < chunk.code.size(); ++i) {
    uint8_t byte = chunk.code[i];
    if (i % 16 == 0) {
      os << std::setw(4) << std::setfill('0') << i << ": ";
    }
    // NOTE: static_cast is a builtin, not imported from anywhere. For reasons
    // I haven't yet figured out, it's safer than using C-style `(int)byte`.
    os << std::hex << std::setw(2) << std::setfill('0')
       << static_cast<int>(byte) << " ";
    if (i % 16 == 15 || i == chunk.code.size() - 1) {
      os << "\n";
    }
  }
  os << std::dec;
  return os;
}

Chunk &Chunk::write(uint8_t byte) {
  try {
    // NOTE: try/catch are (almost?) zero-cost in the non-exception path
    code.push_back(byte);
  } catch (const std::bad_alloc &) {
    // Gracefully handle OOM.
    // NOTE: std::vector will clean up its own memory if an exception is
    // thrown, we don't need to do anything special to avoid leaks!
    throw std::runtime_error("loxc: Out of memory while writing to Chunk");
  }
  return *this;
}
