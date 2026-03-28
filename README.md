# XS

A general-purpose programming language with gradual typing, multiple execution backends, and a plugin system that lets you modify anything. Written in C, builds on Linux, macOS, and Windows with zero dependencies. Started building privately in 2024, made public in early 2026.

```xs
-- types are optional. add them when you want enforcement.
fn fib(n) {
    if n <= 1 { return n }
    return fib(n - 1) + fib(n - 2)
}

fn fib_typed(n: int) -> int {
    if n <= 1 { return n }
    return fib_typed(n - 1) + fib_typed(n - 2)
}

-- closures, pipes, comprehensions
let squares = [x * x for x in 0..10 if x % 2 == 0]
let total = squares |> reduce(fn(a, b) { a + b }, 0)

-- structs with traits
struct Point { x, y }
impl Point {
    fn distance(self) -> f64 {
        return (self.x ** 2 + self.y ** 2) ** 0.5
    }
}

-- pattern matching
fn describe(val) {
    match val {
        0 => "zero"
        x if x < 0 => "negative"
        _ => "positive"
    }
}

-- plugins can add new syntax
use plugin "unless.xs"
unless false { println("this runs") }
```

## Install

Download a prebuilt binary from [releases](https://github.com/xs-lang0/xs/releases), or build from source:

```bash
make            # produces ./xs
make test       # 14 test suites
make release    # optimized build (-O3, LTO, stripped)
make install    # install to /usr/local/bin/xs
```

Needs gcc or clang. No other dependencies. The binary is ~1.5MB and includes everything (HTTPS via embedded BearSSL, no runtime deps).

## Run

```bash
xs file.xs              # run a script
xs                      # interactive REPL
xs -e 'println(42)'     # eval one-liner
xs --vm file.xs         # bytecode VM backend
xs --jit file.xs        # JIT backend (x86-64)
xs --emit js file.xs    # transpile to JavaScript
xs --emit c file.xs     # transpile to C
xs --check file.xs      # static type check without running
xs --strict file.xs     # require type annotations everywhere
```

## What's in the box

**Language features:**
- Gradual typing with `--check` and `--strict`
- Structs, traits, enums, classes with inheritance
- Pattern matching with destructuring and guards
- Closures, generators (`fn*`/`yield`), arrow lambdas
- Algebraic effects (`effect`/`perform`/`handle`/`resume`)
- All the concurrency: spawn, async/await, actors, channels, nurseries
- First-class regex (`/pattern/` literals, `.test()`, `.match()`, `.replace()`)
- List/map comprehensions, spread, pipe operator
- try/catch/finally, defer, throw

**Backends:**
- Tree-walk interpreter (default)
- Bytecode VM (`--vm`, full feature parity, faster for compute-heavy code)
- JIT compiler (x86-64, early stage)
- Transpilers: JavaScript, C, WebAssembly

**Tooling:**
- REPL with syntax highlighting
- LSP server (`xs lsp`)
- Formatter (`xs fmt`), linter (`xs lint`)
- Test runner (`xs test`), benchmarks (`xs bench`)
- Profiler (`xs profile`), coverage (`xs coverage`)
- Package manager (`xs install`, `xs remove`)
- Doc generator (`xs doc`)

**Standard library** (14 modules, all built in):
math, string, time, io, fs, path, random, json, os, collections, re, crypto, fmt, net

**Plugin system:**
Plugins are XS scripts with direct access to the lexer, parser, and runtime. Add keywords, inject globals, hook evaluation, override syntax, intercept imports -- written in XS, not C.

**Networking:**
HTTP/HTTPS client with zero external dependencies (BearSSL embedded for TLS).

## Quick examples

```xs
-- http request
import net
let resp = net.http_get("https://httpbin.org/get")
println(resp["status"])   -- 200

-- file operations
import fs
fs.write("/tmp/hello.txt", "hi from xs")
println(fs.read("/tmp/hello.txt"))

-- actors
actor Counter {
    var count = 0
    fn increment() { count = count + 1 }
    fn get() { count }
}
let c = spawn Counter
c.increment()
c.increment()
println(c.get())  -- 2

-- gradual typing catches mistakes
let nums: [int] = [1, 2, "oops"]  -- runtime error: expected '[int]', got '[mixed]'
```

## Project layout

```
src/            compiler and runtime (C)
src/tls/        embedded BearSSL for HTTPS
tests/          14 test suites
examples/       working examples and plugins
Makefile        build system
LANGUAGE.md     complete language reference
COMMANDS.md     CLI commands and flags
PLUGINS.md      plugin system guide
xs.toml         project config
```

## Docs

- [LANGUAGE.md](LANGUAGE.md) -- full language reference (~2000 lines, covers everything)
- [COMMANDS.md](COMMANDS.md) -- every CLI command, flag, and subcommand
- [PLUGINS.md](PLUGINS.md) -- plugin system guide with working examples
- [STATUS.md](STATUS.md) -- what works, what's partial, what's planned
- [CONTRIBUTING.md](CONTRIBUTING.md) -- how to contribute

## License

Apache 2.0
