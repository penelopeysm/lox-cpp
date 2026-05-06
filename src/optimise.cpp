#include "optimise.hpp"
#include "chunk.hpp"
#include "value.hpp"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#ifdef LOX_DEBUG
#include <iostream>
#endif

namespace lox {
namespace optimise {

bool AddLocalConstOptimisation::matches(const Chunk& chunk, const ChunkInfo& ci,
                                        size_t instruction_index) const {
  if (instruction_index + match_length() - 1 >= ci.instruction_offsets.size()) {
    return false;
  }
  size_t i = instruction_index;
  if (static_cast<OpCode>(chunk.at(ci.instruction_offsets[i])) ==
          OpCode::GET_LOCAL &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 1])) ==
          OpCode::CONSTANT &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 2])) ==
          OpCode::ADD &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 3])) ==
          OpCode::SET_LOCAL &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 4])) ==
          OpCode::POP) {
    uint8_t get_local_index = chunk.at(ci.instruction_offsets[i] + 1);
    uint8_t set_local_index = chunk.at(ci.instruction_offsets[i + 3] + 1);
    if (get_local_index == set_local_index &&
        /* make sure that nothing tries to jump to the middle of this sequence
         */
        !(ci.jump_targets.contains(ci.instruction_offsets[i + 1]) ||
          ci.jump_targets.contains(ci.instruction_offsets[i + 2]) ||
          ci.jump_targets.contains(ci.instruction_offsets[i + 3]) ||
          ci.jump_targets.contains(ci.instruction_offsets[i + 4]))) {
      return true;
    }
  }
  return false;
}

size_t AddLocalConstOptimisation::match_length() const { return 5; }

std::pair<size_t, size_t>
AddLocalConstOptimisation::emit(const Chunk& old_chunk, size_t old_byte_offset,
                                Chunk& new_chunk) const {
  size_t line_number = old_chunk.debuginfo_at(old_byte_offset);
  uint8_t local_index = old_chunk.at(old_byte_offset + 1);
  uint8_t constant_index = old_chunk.at(old_byte_offset + 3);
  new_chunk.write(static_cast<uint8_t>(OpCode::ADD_LOCAL_CONST), line_number);
  new_chunk.write(local_index, line_number);
  new_chunk.write(constant_index, line_number);
  return {8, 3}; // 8 bytes read from old chunk, 3 bytes written to new chunk
}

bool LocalConstLessOptimisation::matches(const Chunk& chunk,
                                         const ChunkInfo& ci,
                                         size_t instruction_index) const {
  if (instruction_index + match_length() - 1 >= ci.instruction_offsets.size()) {
    return false;
  }
  size_t i = instruction_index;
  if (static_cast<OpCode>(chunk.at(ci.instruction_offsets[i])) ==
          OpCode::GET_LOCAL &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 1])) ==
          OpCode::CONSTANT &&
      static_cast<OpCode>(chunk.at(ci.instruction_offsets[i + 2])) ==
          OpCode::LESS) {
    if (!(ci.jump_targets.contains(ci.instruction_offsets[i + 1]) ||
          ci.jump_targets.contains(ci.instruction_offsets[i + 2]))) {
      return true;
    }
  }
  return false;
}

size_t LocalConstLessOptimisation::match_length() const { return 3; }

std::pair<size_t, size_t>
LocalConstLessOptimisation::emit(const Chunk& old_chunk, size_t old_byte_offset,
                                 Chunk& new_chunk) const {
  size_t line_number = old_chunk.debuginfo_at(old_byte_offset);
  uint8_t local_index = old_chunk.at(old_byte_offset + 1);
  uint8_t constant_index = old_chunk.at(old_byte_offset + 3);
  new_chunk.write(static_cast<uint8_t>(OpCode::LOCAL_CONST_LESS), line_number);
  new_chunk.write(local_index, line_number);
  new_chunk.write(constant_index, line_number);
  return {5, 3};
}

ChunkInfo::ChunkInfo(const Chunk& chunk) : jumps(), instruction_offsets() {
  size_t offset = 0;
  while (offset < chunk.size()) {
    // Check if the opcode is a jump
    OpCode instruction = static_cast<OpCode>(chunk.at(offset));
    if (instruction == OpCode::JUMP || instruction == OpCode::JUMP_IF_FALSE) {
      uint8_t high_byte = chunk.at(offset + 1);
      uint8_t low_byte = chunk.at(offset + 2);
      ptrdiff_t jump_offset = lox::get_jump_offset(high_byte, low_byte);
      // check for negative targets (just in case). note that we have to add
      // 3 to the offset here, because we jump after reading the offset
      ptrdiff_t tmp = static_cast<ptrdiff_t>(offset) + 3 + jump_offset;
      if (tmp < 0) {
        std::cerr << offset << " " << jump_offset << "\n";
        throw std::runtime_error("invalid jump to before start of chunk!");
      }
      // if it's fine we can safely cast back to size_t
      size_t target_offset = static_cast<size_t>(tmp);
      jumps[offset] = target_offset;
      jump_targets.insert(target_offset);
    }
    instruction_offsets.push_back(offset);
    offset = next_instruction(chunk, offset);
  }

  // Check that all jump targets are valid instruction offsets
  for (const auto& [_, target_offset] : jumps) {
    if (!std::binary_search(instruction_offsets.begin(),
                            instruction_offsets.end(), target_offset)) {
      throw std::runtime_error("invalid jump target offset " +
                               std::to_string(target_offset) +
                               " is not a valid instruction");
    }
  }

#ifdef LOX_DEBUG
  std::cerr << "collected chunk info \n";
  for (const auto& [jump_offset, target_offset] : jumps) {
    std::cerr << "jump at offset " << jump_offset << " to target offset "
              << target_offset << "\n";
  }
#endif
}

/* Return the offset of the next instruction when linearly scanning through
 * the bytecode. If the current instruction is the last in the chunk,
 * returns the offset of the end of the chunk. */
size_t next_instruction(const Chunk& chunk, size_t offset) {
  size_t nbytes = chunk.size();
  if (offset > nbytes) {
    throw std::out_of_range("loxc: Chunk::disassemble: offset " +
                            std::to_string(offset) + " out of range (size " +
                            std::to_string(nbytes) + ")");
  } else if (offset == nbytes) {
    return offset;
  }

  uint8_t instruction = chunk.at(offset);
  switch (static_cast<OpCode>(instruction)) {

  case OpCode::CLOSURE: {
    // get the ObjFunction from the constant table
    uint8_t constant_index = chunk.at(offset + 1);
    auto function =
        static_cast<ObjFunction*>(as_obj(chunk.constant_at(constant_index)));
    size_t n_upvalues = function->upvalues.size();
    return offset + 2 + 2 * n_upvalues;
  }

  case OpCode::CLOSE_UPVALUE:
  case OpCode::RETURN:
  case OpCode::DEFINE_METHOD:
  case OpCode::NEGATE:
  case OpCode::ADD:
  case OpCode::SUBTRACT:
  case OpCode::MULTIPLY:
  case OpCode::DIVIDE:
  case OpCode::NOT:
  case OpCode::EQUAL:
  case OpCode::GREATER:
  case OpCode::LESS:
  case OpCode::PRINT:
  case OpCode::POP:
  case OpCode::INHERIT: {
    return offset + 1;
  }

  case OpCode::CONSTANT:
  case OpCode::GET_UPVALUE:
  case OpCode::SET_UPVALUE:
  case OpCode::CLASS:
  case OpCode::GET_PROPERTY:
  case OpCode::SET_PROPERTY:
  case OpCode::GET_GLOBAL:
  case OpCode::SET_GLOBAL:
  case OpCode::DEFINE_GLOBAL:
  case OpCode::SET_LOCAL:
  case OpCode::GET_LOCAL:
  case OpCode::CALL:
  case OpCode::GET_SUPER: {
    return offset + 2;
  }

  case OpCode::ADD_LOCAL_CONST:
  case OpCode::LOCAL_CONST_LESS:
  case OpCode::INVOKE:
  case OpCode::SUPER_INVOKE:
  case OpCode::JUMP_IF_FALSE:
  case OpCode::JUMP: {
    return offset + 3;
  }
  }
  throw std::runtime_error("loxc: Chunk::disassemble: unknown opcode " +
                           std::to_string(instruction));
}

std::unordered_map<size_t, size_t> find_optimisation_offsets(
    const Chunk& chunk, const ChunkInfo& ci,
    const std::vector<std::unique_ptr<PeepholeOptimisation>>& registry) {

  std::unordered_map<size_t, size_t> optimisation_offsets;
  size_t i = 0;
  while (i < ci.instruction_offsets.size()) {
    bool found_opt = false;
    for (size_t opt_num = 0; opt_num < registry.size(); opt_num++) {
      if (registry[opt_num]->matches(chunk, ci, i)) {
        optimisation_offsets[ci.instruction_offsets[i]] = opt_num;
        i += registry[opt_num]->match_length();
        found_opt = true;
        break;
      }
    }
    if (!found_opt) {
      i++;
    }
  }
  return optimisation_offsets;
}

Chunk apply_optimisations(
    const Chunk& old_chunk, const ChunkInfo& ci,
    const std::unordered_map<size_t, size_t>& optimisation_offsets,
    const std::vector<std::unique_ptr<PeepholeOptimisation>>& registry) {
  Chunk new_chunk;

  // Rebuild constant table (just the same)
  for (size_t i = 0; i < old_chunk.constants_size(); i++) {
    new_chunk.push_constant(old_chunk.constant_at(i));
  }

  // Build a old offset => new offset map so that we can patch jump offsets
  // later.
  std::unordered_map<size_t, size_t> old_to_new_offset;

  // Rebuild bytecode itself + debuginfo
  size_t old_offset = 0;
  size_t new_offset = 0; // Every time we write to chunk we increment this
  while (old_offset < old_chunk.size()) {
    old_to_new_offset[old_offset] = new_offset;
    size_t line_number = old_chunk.debuginfo_at(old_offset);

    size_t next_old_offset = next_instruction(old_chunk, old_offset);
    auto it = optimisation_offsets.find(old_offset);
    if (it != optimisation_offsets.end()) {
      auto [old_bytes_read, new_bytes_written] =
          registry[it->second]->emit(old_chunk, old_offset, new_chunk);
      old_offset += old_bytes_read;
      new_offset += new_bytes_written;
    } else {
      // just copy the instruction to the new chunk
      for (size_t i = old_offset; i < next_old_offset; i++) {
        new_chunk.write(old_chunk.at(i), line_number);
        new_offset++;
      }
      old_offset = next_old_offset;
    }
  }

  // Patch jump offsets in new chunk
  for (const auto& [old_jump_offset, old_target_offset] : ci.jumps) {
    size_t new_jump_offset = old_to_new_offset.at(old_jump_offset);
    size_t new_target_offset = old_to_new_offset.at(old_target_offset);
    // To patch the jump operand, we need to move one past the opcode itself
    size_t new_jump_operand_offset = new_jump_offset + 1;
    bool success = new_chunk.patch_jump_operand(new_jump_operand_offset,
                                                new_target_offset);
    // Should not fail but we can check anyway
    if (!success) {
      throw std::runtime_error("loxc: apply_optimisations: jump offset from " +
                               std::to_string(new_jump_offset) + " to " +
                               std::to_string(new_target_offset) +
                               " is too large to fit in two bytes");
    }
  }

  return new_chunk;
}

Chunk peephole_optimise(Chunk& chunk) {
#ifdef LOX_NO_OPTIMISE
  return chunk;
#else
  ChunkInfo chunk_info(chunk);
  // NOTE: Can't use initialiser list because it copies whatever is passed to it
  // and unique_ptr can't be copied
  std::vector<std::unique_ptr<PeepholeOptimisation>> registry;
  registry.push_back(std::make_unique<AddLocalConstOptimisation>());
  registry.push_back(std::make_unique<LocalConstLessOptimisation>());

  auto optimisation_offsets =
      find_optimisation_offsets(chunk, chunk_info, registry);
  return apply_optimisations(chunk, chunk_info, optimisation_offsets, registry);
#endif
}

} // namespace optimise
} // namespace lox
