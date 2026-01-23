# XS

One language for everything, gradual by default.

XS is a general-purpose programming language that starts dynamic and lets you
add types when you want them enforced. It has multiple execution backends,
transpiles to other languages, and ships with batteries included.

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
make test     # runs the test suite (12 test files)
make clean    # clean build artifacts
```

Requires a C compiler (gcc or clang).

## Run

```bash
./xs file.xs          # run a script
./xs                  # start the REPL
./xs run file.xs      # explicit run (same as above)
```

## Features

**Type system** -- Gradual typing. Leave types off and everything is dynamic.
Add annotations and they get enforced. Mix freely.

**Backends** -- Tree-walk interpreter (default), bytecode VM, JIT compiler.
Pick what fits.

**Transpilation** -- Compile to JavaScript, C, or WebAssembly.

**Object system** -- Both struct/trait (composition) and class/inheritance (OOP).
Use whichever makes sense.

**Effects** -- Algebraic effects with `effect`, `perform`, `handle`, and `resume`.
Think of them as resumable exceptions.

**Concurrency** -- `spawn`, `async`/`await`, actors, channels, and nurseries.
All of them, not just one.

**Pattern matching** -- `match` expressions with destructuring, guards, and
nested patterns.

**Error handling** -- `try`/`catch` plus `defer` for cleanup.

**Closures and lambdas** -- First-class functions, arrow syntax, captures work
how you'd expect.

**Collections** -- Lists, maps, sets, tuples, ranges, list comprehensions,
spread/rest.

**Built-in tools:**

- REPL
- LSP server
- Formatter
- Linter
- Profiler
- Test runner

## Project layout

```
src/          # compiler and runtime source
tests/        # 12 test files covering all language features
Makefile      # build system
xs.toml       # project config
LANGUAGE.md   # full language reference
COMMANDS.md   # full CLI reference
```

## Reference

- [LANGUAGE.md](LANGUAGE.md) -- complete language spec with examples
- [COMMANDS.md](COMMANDS.md) -- every CLI command and flag

## Author

[@xslang9](https://github.com/xslang9)

## License

See repository for license details.
