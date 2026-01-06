#pragma once

#include "value.hpp"
#include <cstdlib>
#include <cstdint>
#include <ostream>
#include <vector>

namespace lox {

// NOTE: `enum class` means that you always have to qualify the names with the
// enum class name, e.g., OpCode::RETURN. If you use `enum` instead, then
// `RETURN` would be in the top-level scope.
enum class OpCode {
  CONSTANT,
  RETURN,
  NEGATE,
  ADD,
  SUBTRACT,
  MULTIPLY,
  DIVIDE,
  NOT,
  EQUAL,
  GREATER,
  LESS,
  PRINT,
  POP,
  GET_GLOBAL,
  SET_GLOBAL,
  DEFINE_GLOBAL,
  SET_LOCAL,
  GET_LOCAL,
  // more to come
};

struct DebugInfo {
  size_t bytecode_offset;
  size_t line;
  // size_t column;
};

class Chunk {
public:
  Chunk();
  size_t size() const;
  size_t capacity() const;
  uint8_t at(size_t index) const;
  Chunk& write(OpCode opcode, size_t line);
  Chunk& write(uint8_t byte, size_t line);
  Chunk& reset();
  // Returns the index of the constant just added
  size_t push_constant(lox::Value value);
  lox::Value constant_at(size_t index) const;
  size_t constants_size() const;
  size_t debuginfo_size() const;
  size_t debuginfo_at(size_t bytecode_offset) const;

  std::ostream& hex_dump(std::ostream& os) const;
  std::ostream& disassemble(std::ostream& os) const;
  // Disassemble a single instruction at the given offset, and return the new
  // offset.
  size_t disassemble(std::ostream& os, size_t offset) const;

private:
  // NOTE: std::vector will resize itself automatically when push_back needs
  // it. It has two different notions: size() is the number of elements stored,
  // capacity() is the number of elements it can store without resizing.
  std::vector<uint8_t> code;
  std::vector<lox::Value> constants;
  std::vector<DebugInfo> debuginfo;
};

// NOTE: This USED to be a `friend` method on Chunk. That would allows the
// implementation of this function to access private members of Chunk (such as
// `code`) which is necessary for printing. Note that member functions (like
// `write` above) don't have such restrictions: they can always access private
// members of their own class. However, member functions also get an implicit
// `this` parameter as the first argument, whereas friend functions do not. We
// COULD make operator<< a member function, something like std::ostream
// &operator<<(std::ostream &os) const; but that would make its usage more
// awkward, since we would have to write `chunk << std::cout` instead of the
// more natural `std::cout << chunk`.
//
// Note that the implementation of this function needs to use
// `lox::operator<<`. It's not the same function as `std::operator<<`! But the
// compiler will be smart enough to figure out which one to call based on the
// argument types ('argument-dependent lookup').
std::ostream& operator<<(std::ostream& os, const Chunk& chunk);

} // namespace lox
