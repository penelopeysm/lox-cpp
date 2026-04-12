#pragma once
#include "chunk.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lox {

namespace optimise {

struct AddLocalConstInfo {
  uint8_t local_index;
  uint8_t constant_index;
};

class ChunkInfo {
public:
  /* Map from jump instruction offset to the target instruction offset */
  std::unordered_map<size_t, size_t> jumps;
  /* the targets themselves, for quick lookup */
  std::unordered_set<size_t> jump_targets;
  /* Collection of all offsets */
  std::vector<size_t> instruction_offsets;

  ChunkInfo(const Chunk& chunk);
};

size_t next_instruction(const Chunk& chunk, size_t offset);

Chunk peephole_optimise(Chunk& chunk);

} // namespace optimise
} // namespace lox
