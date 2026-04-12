#pragma once
#include "chunk.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lox {

namespace optimise {

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

class PeepholeOptimisation {
public:
  virtual bool matches(const Chunk& chunk, const ChunkInfo& ci,
                       size_t offset) const = 0;
  virtual size_t match_length() const = 0;
  // Returns a pair of (number of bytes in old chunk overwritten, number of
  // bytes written to new chunk)
  virtual std::pair<size_t, size_t>
  emit(const Chunk& old_chunk, size_t old_offset, Chunk& new_chunk) const = 0;
  virtual ~PeepholeOptimisation() = default;
};

class AddLocalConstOptimisation : public PeepholeOptimisation {
public:
  bool matches(const Chunk& chunk, const ChunkInfo&,
               size_t offset) const override;
  size_t match_length() const override;
  std::pair<size_t, size_t> emit(const Chunk& old_chunk, size_t old_offset,
                                 Chunk& new_chunk) const override;
};

class LocalConstLessOptimisation : public PeepholeOptimisation {
public:
  bool matches(const Chunk& chunk, const ChunkInfo&,
               size_t offset) const override;
  size_t match_length() const override;
  std::pair<size_t, size_t> emit(const Chunk& old_chunk, size_t old_offset,
                                 Chunk& new_chunk) const override;
};

size_t next_instruction(const Chunk& chunk, size_t offset);

Chunk peephole_optimise(Chunk& chunk);

} // namespace optimise
} // namespace lox
