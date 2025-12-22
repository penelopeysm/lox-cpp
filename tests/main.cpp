#include "chunk.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Sample test case") {
  lox::Chunk chunk;
  REQUIRE(chunk.size() == 0);
}
