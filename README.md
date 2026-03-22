# XS

A general-purpose programming language with gradual typing and multiple backends. Made privately starting 2024, published in early-mid stage 2026.

```xs
-- no types required, but you can add them
fn greet(name) {
    print("Hello, {name}!")
}

fn greet_typed(name: str) -> str {
    return "Hello, {name}!"
}

-- closures, pattern matching, the usual
let nums = [1, 2, 3, 4, 5]
let doubled = nums.map((x) => x * 2)

-- algebraic effects
effect Ask {
    fn prompt(msg) -> str
}

let result = handle ask_user() {
    Ask.prompt(msg) => resume("World")
}
```

## Build

```bash
make          # produces ./xs
make test     # runs the test suite
make clean    # clean build artifacts
```

Requires a C compiler (gcc or clang). Builds on Linux, macOS, and Windows (MinGW).

## Run

```bash
./xs file.xs          # run a script
./xs                  # start the REPL
./xs run file.xs      # explicit run (same as above)
```

## What works well

**Gradual typing** -- Leave types off and everything is dynamic. Add annotations and they get enforced at runtime. `--strict` mode requires annotations everywhere, `--check` does static analysis.

**Tree-walk interpreter** -- The default backend. Handles all language features, including the weirder ones like algebraic effects and the plugin system.

**Object system** -- Both struct/trait (composition) and class/inheritance (OOP). Use whichever makes sense for the problem.

**Pattern matching** -- `match` expressions with destructuring, guards, nested patterns. Works well.

## What exists but is rough

**Bytecode VM** -- Works for basic programs. Doesn't support everything the tree-walk interpreter does yet.

**JIT compiler** -- x86-64 only, handles arithmetic and simple functions. Very early stage.

**Transpilation** -- JS backend is the most complete. C backend handles a decent chunk. WASM is very early, basically just arithmetic.

**Concurrency** -- `spawn`, `async`/`await`, actors, channels, nurseries are all there. Everything is cooperative (no real threads), spawn just runs immediately. It works, but it's not going to win any benchmarks.

**Algebraic effects** -- `effect`, `perform`, `handle`, `resume`. Covers the common cases, edge cases may still have bugs.

**Plugin system** -- Plugins can inject globals, hook into the parser, add keywords, override syntax. Pretty powerful but the API is still shifting.

## Built-in tools

REPL, LSP server, formatter, linter, profiler, test runner.

## Project layout

```
src/          # compiler and runtime source
tests/        # test files
Makefile      # build system
xs.toml       # project config
LANGUAGE.md   # language reference
COMMANDS.md   # CLI reference
```

## Reference

- [LANGUAGE.md](LANGUAGE.md) -- language spec with examples
- [COMMANDS.md](COMMANDS.md) -- CLI commands and flags

## Author

[@xs-lang0](https://github.com/xs-lang0)

## License

Apache 2.0
