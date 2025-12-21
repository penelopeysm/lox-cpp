# lox-cpp

```bash
make
./loxc
```

## generating `compile_commands.json`

this is needed to make clang-based tools (like clangd) work properly.
download [bear](https://github.com/rizsotto/Bear) (e.g. via Homebrew) and run:

```bash
bear -- make
```
