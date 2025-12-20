#pragma once

#include <cstdint>
#include <vector>

enum OpCode {
  OP_RETURN,
  // more to come
};

class Chunk {
  // NOTE: std::vector will resize itself automatically when push_back needs
  // it. It has two different notions: size() is the number of elements stored,
  // capacity() is the number of elements it can store without resizing.
  std::vector<uint8_t> code;

public:
  Chunk();

  Chunk &write(uint8_t byte);

  // NOTE: `friend` allows this function to access private members of Chunk
  // (such as `code`) which is necessary for printing.
  friend std::ostream &operator<<(std::ostream &os, const Chunk &chunk);
};
