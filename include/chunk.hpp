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

  void display() const;

  Chunk& write(uint8_t byte);
};
