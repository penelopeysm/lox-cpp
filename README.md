# lox-cpp

```bash
make
./loxc
```

## generating `compile_commands.json`

this is needed to make clang-based tools (like clangd) work properly.

```bash
brew install bear
bear -- make
```

## tests

```bash
brew install pkg-config catch2
make test
```
