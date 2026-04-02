# XS Status

What works, what's partial, and what's planned. Updated for v0.3.7.

## Tree-Walk Interpreter

The default backend. Handles the full language.

| Feature | Status |
|---------|--------|
| Variables (let, var, const) | works |
| All data types (int, float, str, bool, null, array, map, tuple, range, re) | works |
| Arithmetic, bitwise, logical operators | works |
| String interpolation, escapes, methods | works |
| Control flow (if/elif/else, for, while, loop, match, break, continue) | works |
| Pattern matching with destructuring, guards, nested patterns | works |
| Functions, closures, default params, variadic, arrow lambdas | works |
| Function overloading (dispatch by arity) | works |
| Tagged blocks (user-defined control structures) | works |
| Reactive bindings (bind) | works |
| Gradual contracts (where clauses) | works |
| Adapt functions (multi-target) | works |
| Inline C blocks (for C transpiler) | works |
| Generators (fn*/yield) | works |
| Structs, impl, traits | works |
| Enums with associated data | works |
| Classes with inheritance | works |
| Algebraic effects (effect/perform/handle/resume) | works |
| Concurrency (spawn, async/await, channels, actors, nurseries) | works |
| Error handling (try/catch/finally, throw, defer) | works |
| Modules and imports | works |
| List/map comprehensions | works |
| Pipe operator | works |
| Gradual typing (--check, --strict) | works |
| Plugin system | works |
| Standard library (14 modules) | works |
| HTTPS via embedded BearSSL | works |
| Universal literals (duration, color, date, size, angle) | works |
| Temporal primitives (every, after, timeout, debounce) | works |

| Multi-line strings (triple-quote) | works |
| `do` expressions | works |
| `with` resource management | works |
| Named arguments | works |
| Enum methods via impl | works |

All 18 test suites pass on Linux, macOS, and Windows.

## Bytecode VM

Use `--vm` flag. Full feature parity with the interpreter (except reactive bindings, which evaluate once).

| Feature | Status |
|---------|--------|
| Arithmetic, variables, functions | works |
| Closures and upvalues | works |
| Control flow (if, while, for, loop, match) | works |
| Labeled break/continue | works |
| Arrays, maps, tuples, ranges | works |
| String interpolation | works |
| Pattern matching (literals, guards, tuples, enums, structs) | works |
| Functions with default params, variadic | works |
| Structs with impl methods, spread | works |
| Classes with inheritance and super | works |
| Traits | works |
| Enums with data and matching | works |
| Concurrency (spawn, channels, actors, async/await, nursery) | works |
| Algebraic effects (perform/handle/resume) | works |
| Error handling (try/catch/finally, throw, defer) | works |
| Modules and imports | works |
| List/map comprehensions | works |
| Pipe operator | works |
| Plugin system (use plugin, global.set, add_method) | works |
| All string methods (80+) | works |
| All array methods (50+) | works |
| All map methods (20+) | works |
| Number methods (is_even, digits, to_hex, etc.) | works |
| Result/Option methods (unwrap, is_ok, etc.) | works |
| Optional chaining (?.) | works |
| Range indexing (arr[1..3]) | works |
| All builtins matching interpreter | works |

| Growable stack and frames (no fixed limits) | works |

All 18 test suites pass. Use `xs build file.xs` to compile, `xs run file.xsc` to execute.

## JIT Compiler

x86-64 only. Early stage, handles basic arithmetic and function calls.

| Feature | Status |
|---------|--------|
| Integer arithmetic | works |
| Function calls | works |
| Loops | works |
| Everything else | falls back to VM |

## C Transpiler

`xs --emit c file.xs` generates standalone C that compiles with gcc/clang.

| Feature | Status |
|---------|--------|
| Variables, arithmetic, control flow | works |
| Functions, default params, expression bodies | works |
| Strings, interpolation, string methods | works |
| Arrays, maps, array methods (map/filter/reduce) | works |
| Structs with impl methods | works |
| Enums with constructors and matching | works |
| Pattern matching with guards | works |
| Channels, actors, spawn, nursery | works |
| Async/await (sequential) | works |
| Closures capturing mutable state | partial: works for single-scope files |
| Generators | not yet |
| Algebraic effects | not yet |
| Plugins | not supported (requires runtime) |

## JavaScript Transpiler

`xs --emit js file.xs` generates Node.js-compatible JavaScript.

| Feature | Status |
|---------|--------|
| Variables, functions, control flow | works |
| Closures, arrow lambdas | works |
| Arrays, maps | works |
| Concurrency | partial |
| Everything else | rough |

## WebAssembly Transpiler

`xs --emit wasm file.xs`: early stage.

| Feature | Status |
|---------|--------|
| Basic arithmetic, function calls | works |
| Everything else | not yet |

## Tooling

| Tool | Status |
|------|--------|
| REPL with syntax highlighting | works |
| LSP server (hover, completion, diagnostics, definition, references, rename, formatting, signature help) | works |
| DAP debugger (breakpoints, stepping, variable inspection, evaluate) | works |
| VSCode extension | works: available on marketplace |
| Formatter (`xs fmt`) | works |
| Linter (`xs lint`) | works |
| Test runner (`xs test`) | works |
| Benchmarks (`xs bench`) | works |
| Execution tracer (`xs --record`, `xs replay`) | works |
| Profiler (`xs profile`) | works |
| Coverage (`xs coverage`) | works |
| Doc generator (`xs doc`) | works |
| Package manager (`xs install/remove/update`) | basic: registry not live |

## Platform Support

| Platform | Status |
|----------|--------|
| Linux (x86-64) | fully tested |
| macOS (x86-64, ARM) | works, CI tested |
| Windows (MinGW) | works, CI tested, static linked |

## Recent Changes (v0.3.7)

- Added: multi-line string literals with triple-quote syntax (`"""..."""`)
- Added: `do` expressions for block-scoped computation (`let x = do { ... }`)
- Added: `with` resource management (`with expr as name { ... }` calls `.close()` on exit)
- Added: named arguments at call sites (`connect(host: "localhost", port: 8080)`)
- Added: enum methods via `impl` blocks
- Added: `.chars()` and `.bytes()` string methods, `.entries()` map method
- Added: tuple destructuring in `for` loops over arrays
- Added: growable VM stacks and frame arrays (no fixed limits)
- Added: REPL `:test` command to run test files
- Added: `xs init` for initializing projects in existing directories
- Added: `xs test --watch` for auto-re-running tests on changes
- Added: benchmark suite (fibonacci, sorting, string ops)
- Added: negative test suite (error validation)
- Added: transpiler integration tests (C backend)
- Improved: JS transpiler (generators, structs/classes, pattern matching)
- Improved: parser error suggestions (semicolons, `console.log`, `===`, etc.)
- Updated: STATUS.md to current version

## Changes in v0.3.0-v0.3.5

- Added: WASM playground build
- Added: universal literals, temporal primitives, reactive bindings
- Added: gradual contracts, adapt functions
- Added: function overloading, tagged blocks, regex type
- Added: execution tracer, HM type inference, bigint auto-promotion
- Fixed: JS transpiler classes, builtins, dedup returns
- Fixed: VM buffer overflows and strbuf realloc leak

## Known Limitations

- Struct operator overloading only works when both operands are structs (not mixed struct+int)
- C transpiler closures break when the same variable name is captured in multiple functions in one file
- JIT is x86-64 only and very early
- WASM transpiler only handles basic programs
- Package registry is not live: `xs install` works with local paths
- VM effects use snapshot/restore (single-shot only, no nested effects)
- VM actors use flattened state (not full closure capture like interpreter)
- Regex uses POSIX extended syntax only (no `\d`, `\w` shorthand - use `[0-9]`, `[a-zA-Z_]`)
