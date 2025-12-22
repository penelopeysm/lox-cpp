#pragma once

#include "value.hpp"
#include <cstdint>
#include <vector>

namespace lox {

// NOTE: `enum class` means that you always have to qualify the names with the
// enum class name, e.g., OpCode::RETURN. If you use `enum` instead, then
// `RETURN` would be in the top-level scope.
enum class OpCode {
  CONSTANT,
  RETURN,
  // more to come
};

struct DebugInfo {
  size_t bytecode_offset;
  size_t line;
  // size_t column;
};

class Chunk {
  // NOTE: std::vector will resize itself automatically when push_back needs
  // it. It has two different notions: size() is the number of elements stored,
  // capacity() is the number of elements it can store without resizing.

  std::vector<uint8_t> code;
  std::vector<lox::Value> constants;
  std::vector<DebugInfo> debug_info;

public:
  Chunk();
  size_t size() const;
  size_t capacity() const;
  Chunk& write(OpCode opcode, size_t line);
  Chunk& write(uint8_t byte, size_t line);
  Chunk& reset();
  Chunk& push_constant(lox::Value value);
  std::ostream& hex_dump(std::ostream& os) const;

  // NOTE: `friend` allows the implementation of this function to access
  // private members of Chunk (such as `code`) which is necessary for printing.
  // Note that member functions (like `write` above) don't have such
  // restrictions: they can always access private members of their own class.
  // However, member functions also get an implicit `this` parameter as the
  // first argument, whereas friend functions do not. We COULD make operator<<
  // a member function, something like
  //   std::ostream &operator<<(std::ostream &os) const;
  // but that would make its usage more awkward, since we would have to write
  // `chunk << std::cout` instead of the more natural `std::cout << chunk`.
  friend std::ostream& operator<<(std::ostream& os, const Chunk& chunk);
};

} // namespace lox
