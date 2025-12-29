# lox-cpp

```bash
make
./loxc
```

by default it builds a debug version, use `make BUILD=release` to disable that

## generating `compile_commands.json`

this is needed to make clang-based tools (like clangd) work properly.

```bash
brew install bear
make clean
bear -- make test
```

note that building the test target also builds the main programme (so this ensures that `compile_commands.json` will contain entries for both the main programme and the tests).

## tests

```bash
brew install pkg-config catch2
make test
```
