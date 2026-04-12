#pragma once

#include "opcode_def.hpp"
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
#define OPCODE_ENUM(name) name,
  OPCODE_LIST(OPCODE_ENUM)
#undef OPCODE_ENUM
};

struct DebugInfo {
  size_t bytecode_offset;
  size_t line;
  // size_t column;
};

inline ptrdiff_t get_jump_offset(uint8_t high_byte, uint8_t low_byte) {
  // NOTE: we have to be really careful here about how we do the static_cast!
  // The high and low bytes are encoded including the sign information as the
  // most significant bit of the high byte. So we need to combine them into a
  // single int16_t FIRST, so that the sign information is in the right place
  // (i.e. the most significant bit of the int16_t), and then only convert to
  // ptrdiff_t.
  //
  // If we did
  //   static_cast<ptrdiff_t>(high_byte) << 8 | low_byte
  //
  // the sign bit would not be in the most significant bit of the ptrdiff_t, and
  // so we would end up with a completely different number.
  int16_t jump_offset = (static_cast<int16_t>(high_byte) << 8) | low_byte;
  return static_cast<ptrdiff_t>(jump_offset);
}
inline std::pair<uint8_t, uint8_t> split_jump_offset(int16_t offset) {
  uint8_t high_byte = static_cast<uint8_t>((offset >> 8) & 0xff);
  uint8_t low_byte = static_cast<uint8_t>(offset & 0xff);
  return {high_byte, low_byte};
}

class Chunk {
public:
  Chunk();
  size_t size() const;
  size_t capacity() const;
  uint8_t at(size_t index) const;
  Chunk& write(OpCode opcode, size_t line);
  Chunk& write(uint8_t byte, size_t line);
  Chunk& patch_at_offset(size_t offset, uint8_t byte);
  // Returns whether the patch was successful (i.e. whether the jump offset fits in two
  // uint8_t's).
  bool patch_jump_operand(size_t jump_operand_byte, size_t target_byte);
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

  // Exposes internals -- for VM use.
  uint8_t* location_at(size_t offset) { return code.data() + offset; }
  uint8_t* begin_location() { return code.data(); }
  uint8_t* end_location() { return code.data() + code.size(); }

private:
  // NOTE: std::vector will resize itself automatically when push_back needs
  // it. It has two different notions: size() is the number of elements stored,
  // capacity() is the number of elements it can store without resizing.
  std::vector<uint8_t> code;
  std::vector<lox::Value> constants;
  std::vector<DebugInfo> debuginfo;
};

} // namespace lox
