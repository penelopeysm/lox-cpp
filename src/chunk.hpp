#pragma once

#include <cstdint>
#include <vector>

enum OpCode {
  OP_RETURN,
  // more to come
};

class Chunk {
  std::vector<uint8_t> code;

public:
  Chunk();

  friend std::ostream &operator<<(std::ostream &os, const Chunk &chunk);

  Chunk &write(uint8_t byte);
};
