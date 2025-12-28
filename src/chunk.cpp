#include "chunk.hpp"
#include "value.hpp"
#include <cstdlib>
#include <format>
#include <iostream>

// NOTE: This is an 'anonymous namespace': everything defined inside here can
// be used in the same file without qualification, but is not visible outside
// this file. This helps to avoid e.g. name clashes with other translation
// units.
namespace {
std::ostream& print_offset(std::ostream& os, size_t offset) {
  // NOTE: The old-style way of printing formatted output was to set flags on
  // the output stream `os`.
  //   return os << std::setw(4) << std::setfill('0') << offset << ": ";
  // This still works, but the problem is that it changes the state of the
  // output stream, so subsequent output may end up being zero-padded as well.
  // In C++20 you can use std::format instead which does not have this problem.
  return os << std::format("{:04} ", offset);
}

std::ostream& print_byte(std::ostream& os, uint8_t byte) {
  // NOTE: static_cast is a builtin, not imported from anywhere. For reasons
  // I haven't yet figured out, it's safer than using C-style `(int)byte`.
  return os << std::format("{:02x} ", byte);
}

} // namespace

lox::Chunk::Chunk() : code(), constants(), debuginfo() {}

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

uint8_t lox::Chunk::at(size_t index) const { return code.at(index); }

lox::Chunk& lox::Chunk::write(uint8_t byte, size_t line) {
  // NOTE: try/catch are (almost?) zero-cost in the non-exception path
  try {
    size_t nbytes = code.size();
    code.push_back(byte);
    // Check if the line number has changed since the last recorded DebugInfo;
    // if so, we need to add a new entry.
    if (debuginfo.empty() || debuginfo.back().line != line) {
      debuginfo.push_back(DebugInfo(nbytes, line));
    }
  } catch (const std::bad_alloc&) {
    // Gracefully handle OOM.
    // NOTE: std::vector will clean up its own memory if an exception is
    // thrown, we don't need to do anything special to avoid leaks!
    throw std::runtime_error("loxc: Out of memory while writing to Chunk");
  }
  return *this;
}

lox::Chunk& lox::Chunk::write(OpCode opcode, size_t line) {
  return lox::Chunk::write(static_cast<uint8_t>(opcode), line);
}

lox::Chunk& lox::Chunk::reset() {
  // NOTE: clear() removes elements and so size() will return 0, but does not
  // change capacity
  code.clear();
  return *this;
}

lox::Chunk& lox::Chunk::push_constant(lox::Value value) {
  try {
    constants.push_back(value);
  } catch (const std::bad_alloc&) {
    throw std::runtime_error(
        "loxc: Out of memory while adding constant to Chunk");
  }
  return *this;
}

lox::Value lox::Chunk::constant_at(size_t index) const {
  return constants.at(index);
}

size_t lox::Chunk::constants_size() const { return constants.size(); }

size_t lox::Chunk::debuginfo_size() const { return debuginfo.size(); }

bool compare_by_offset(const lox::DebugInfo& info, size_t target) {
  return info.bytecode_offset < target;
}

size_t lox::Chunk::debuginfo_at(size_t bytecode_offset) const {
  auto it = std::lower_bound(debuginfo.begin(), debuginfo.end(),
                             bytecode_offset, compare_by_offset);
  if (it->bytecode_offset == bytecode_offset) {
    return it->line;
  } else if (it == debuginfo.begin()) {
    throw std::runtime_error(
        "loxc: debuginfo_at: no debug info for given bytecode offset");
  } else {
    --it;
    return it->line;
  }
}

size_t lox::Chunk::disassemble(std::ostream& os, size_t offset) const {
  size_t nbytes = code.size();
  if (offset >= nbytes) {
    throw std::out_of_range("loxc: Chunk::disassemble: offset out of range");
  }
  print_offset(os, offset);
  uint8_t instruction = code[offset];

  switch (static_cast<OpCode>(instruction)) {
  case OpCode::CONSTANT: {
    uint8_t constant_index = code[offset + 1];
    Value constant = constants[constant_index];
    os << "CONSTANT " << constant << "\n";
    return offset + 2;
  }
  case OpCode::RETURN: {
    os << "RETURN\n";
    return offset + 1;
  }
  case OpCode::NEGATE: {
    os << "NEGATE\n";
    return offset + 1;
  }
  case OpCode::ADD: {
    os << "ADD\n";
    return offset + 1;
  }
  case OpCode::SUBTRACT: {
    os << "SUBTRACT\n";
    return offset + 1;
  }
  case OpCode::MULTIPLY: {
    os << "MULTIPLY\n";
    return offset + 1;
  }
  case OpCode::DIVIDE: {
    os << "DIVIDE\n";
    return offset + 1;
  }
  }
}

std::ostream& lox::Chunk::disassemble(std::ostream& os) const {
  size_t nbytes = size();
  os << "--- Disassembly of Chunk with " << nbytes << " bytes ---" << "\n";
  size_t offset = 0;
  while (offset < nbytes) {
    offset = disassemble(os, offset);
  }
  // We SHOULD have printed all bytes, so this is a sanity check.
  if (offset != nbytes) {
    throw std::runtime_error(
        "loxc: Disassembly error: did not consume all bytes");
  }
  return os;
}

std::ostream& lox::operator<<(std::ostream& os, const lox::Chunk& chunk) {
  chunk.disassemble(os);
  return os;
}

std::ostream& lox::Chunk::hex_dump(std::ostream& os) const {
  size_t nbytes = code.size();
  // NOTE: "\n" vs std::endl: the latter always flushes the stream, which is
  // not always what we want (e.g. if writing to a file, it may be more
  // efficient to buffer).
  os << "--- Hex dump of Chunk with " << nbytes << " bytes ---" << "\n";
  for (size_t offset = 0; offset < nbytes; ++offset) {
    uint8_t byte = code[offset];
    if (offset % 16 == 0)
      print_offset(os, offset);
    print_byte(os, byte);
    if (offset % 16 == 15 || offset == nbytes - 1) {
      os << "\n";
    }
  }
  os << std::dec;
  return os;
}
