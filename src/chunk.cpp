#include "chunk.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>

lox::Chunk::Chunk() : code() {}

// NOTE: Sometimes, tiny functions like these are defined inside the header.
// There are a few reasons why one might do that:
//   1. Inlining: Because header files are included directly in source files,
//      the compiler can see the full definition of Chunk::size(), and can
//      potentially inline the definition, leading to improved performance.
//      This is not possible if Chunk::size() is defined in a separate source
//      file, because each cpp file is its own compilation unit (the idea being
//      that if you change a definition in a cpp file, you shouldn't have to
//      recompile every other cpp file -- whereas if you change a header file,
//      you have to recompile every cpp file that includes it).
//      (In fact, some compilers can do "link-time optimisation" which inlines
//      functions across compilation units. But we won't worry about that.)
//   2. Template classes. I don't understand this fully yet.
//   3. "Header-only libraries": some libraries are designed to be used by only
//      importing a header, instead of having cpp files which must be compiled
//      and linked against. If that is the intent, then all definitions must be
//      contained in the header files.
size_t lox::Chunk::size() const { return code.size(); }

size_t lox::Chunk::capacity() const { return code.capacity(); }

lox::Chunk &lox::Chunk::write(OpCode byte) {
  // NOTE: try/catch are (almost?) zero-cost in the non-exception path
  try {
    code.push_back(static_cast<uint8_t>(byte));
  } catch (const std::bad_alloc &) {
    // Gracefully handle OOM.
    // NOTE: std::vector will clean up its own memory if an exception is
    // thrown, we don't need to do anything special to avoid leaks!
    throw std::runtime_error("loxc: Out of memory while writing to Chunk");
  }
  return *this;
}

lox::Chunk &lox::Chunk::reset() {
  // NOTE: clear() removes elements and so size() will return 0, but does not
  // change capacity
  code.clear();
  return *this;
}

std::ostream &lox::operator<<(std::ostream &os, const lox::Chunk &chunk) {
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
