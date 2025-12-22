#include "chunk.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Chunk") {
  lox::Chunk chunk;
  REQUIRE(chunk.size() == 0);
  REQUIRE(chunk.constants_size() == 0);
  REQUIRE(chunk.debuginfo_size() == 0);

  size_t line = 123;
  chunk.write(lox::OpCode::RETURN, line);
  REQUIRE(chunk.size() == 1);
  REQUIRE(chunk.debuginfo_size() == 1);
  REQUIRE(chunk.debuginfo_at(0) == line);

  size_t line2 = 124;
  chunk.write(lox::OpCode::CONSTANT, line2);
  chunk.write(0, line2);
  chunk.push_constant(3.14);
  REQUIRE(chunk.constants_size() == 1);
  REQUIRE(chunk.size() == 3);
}
