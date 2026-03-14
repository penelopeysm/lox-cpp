# lox-cpp

C++20 implementation of the bytecode VM from [Crafting Interpreters](https://craftinginterpreters.com/).

## Build and run

```bash
make
./loxc <file>
```

By default it builds a debug version; use `make BUILD=release` to disable that.

## Generating `compile_commands.json`

This is needed to make clang-based tools (like clangd) work properly:

```bash
brew install bear
make clean
bear -- make test
```

Note that building the test target also builds the main programme (so this ensures that `compile_commands.json` will contain entries for both the main programme and the tests).

## Tests

```bash
brew install pkg-config catch2
make test
```

## Differences from the book

I allow

```
{
  var a = 1;
  {
    var a = a;
  }
}
```

The initialiser on the right hand side refers to the outer `a`, not itself. Original Lox throws an error in this case.
