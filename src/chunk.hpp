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

  size_t size() const;
  size_t capacity() const;
  Chunk &write(uint8_t byte);
  Chunk &reset();

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
  friend std::ostream &operator<<(std::ostream &os, const Chunk &chunk);
};
