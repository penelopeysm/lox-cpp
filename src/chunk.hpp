#pragma once

#include "value_def.hpp"
#include <cstdint>
#include <cstdlib>
#include <ostream>
#include <string_view>
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
  JUMP_IF_FALSE,
  JUMP,
  CALL,
  CLOSURE,
  GET_UPVALUE,
  SET_UPVALUE,
  CLOSE_UPVALUE,
  CLASS,
  GET_PROPERTY,
  SET_PROPERTY,
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
  Chunk& patch_at_offset(size_t offset, uint8_t byte);
  Chunk& reset();
  // Returns the index of the constant just added
  size_t push_constant(lox::Value value);
  lox::Value constant_at(size_t index) const;
  size_t constants_size() const;
  size_t debuginfo_size() const;
  size_t debuginfo_at(size_t bytecode_offset) const;

  const std::vector<lox::Value>& get_constants() const { return constants; }

  std::ostream& hex_dump(std::ostream& os, std::string_view fn_name) const;
  // Disassemble a single instruction at the given offset, and return the new
  // offset.
  size_t disassemble(std::ostream& os, size_t offset,
                     std::string_view fn_name) const;

private:
  // NOTE: std::vector will resize itself automatically when push_back needs
  // it. It has two different notions: size() is the number of elements stored,
  // capacity() is the number of elements it can store without resizing.
  std::vector<uint8_t> code;
  std::vector<lox::Value> constants;
  std::vector<DebugInfo> debuginfo;
};

} // namespace lox
