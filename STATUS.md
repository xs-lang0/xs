# XS Status

What works, what's partial, and what's planned. Updated for v0.3.1.

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

All 14 test suites pass on Linux, macOS, and Windows.

## Bytecode VM

Use `--vm` flag. Full feature parity with the interpreter.

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

All 14 test suites pass. Use `xs build file.xs` to compile, `xs run file.xsc` to execute.

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

## Recent Changes (v0.3.1)

- Added: regex as a first-class type (`re`) with `/pattern/` literal syntax
- Added: regex methods: `.test()`, `.match()`, `.replace()`, `.source()`
- Added: regex patterns in `match` expressions
- Added: execution tracer (`xs --record trace.xst script.xs`, `xs replay trace.xst`)
- Added: `--trace-deep` flag for JSON serialization of complex values in traces
- Added: HM type inference wired into `--check` (catches type errors in unannotated code)
- Added: bigint auto-promotion on overflow (`type()` returns "int", `is_int()` returns true)
- Added: `for (k, v) in map` direct key-value iteration without `.entries()`
- Fixed: struct operator overloading now works for all operators (was broken for `*`)
- Fixed: flag stacking (flags work before or after filename, `--check` + `--vm` works)
- Test suite now at 14 test files

## Known Limitations

- Struct operator overloading only works when both operands are structs (not mixed struct+int)
- C transpiler closures break when the same variable name is captured in multiple functions in one file
- JIT is x86-64 only and very early
- WASM transpiler only handles basic programs
- Package registry is not live: `xs install` works with local paths
- VM effects use snapshot/restore (single-shot only, no nested effects)
- VM actors use flattened state (not full closure capture like interpreter)
- Regex uses POSIX extended syntax only (no `\d`, `\w` shorthand - use `[0-9]`, `[a-zA-Z_]`)
