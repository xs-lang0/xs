# XS Plugin System

Guide to writing and using plugins in XS. Plugins let you inject globals, add methods to built-in types, define new syntax, hook into the interpreter, and generally reshape the language to fit your domain. If you've used Babel plugins or Rust proc macros, same idea -- but simpler.

> This doc assumes you know the basics of XS. See LANGUAGE.md for the language reference.

---

## What's a Plugin?

A plugin is a regular `.xs` file that gets access to a special `plugin` object. That object has surfaces for the runtime, parser, lexer, and AST -- you use them to extend the host program that loaded you.

```xs
-- my_plugin.xs
plugin.meta = #{ name: "my_plugin", version: "0.1.0" }

plugin.runtime.global.set("greet", fn(name) {
    return "Hello, {name}!"
})
```

```xs
-- main.xs
use plugin "my_plugin.xs"
println(greet("world"))  -- Hello, world!
```

That's it. The plugin file runs in a temporary interpreter, but the hooks and globals it registers affect the host.

---

## Loading Plugins

```xs
use plugin "path/to/plugin.xs"
```

The path is resolved relative to the file doing the loading. Plugins execute top-to-bottom at load time, before the rest of your program runs. If a plugin fails to load (syntax error, runtime error, missing dependency), the host program stops -- you won't get half-loaded state.

Multiple plugins load in order. Later plugins can override globals set by earlier ones.

---

## The Plugin Object

Every plugin file gets a `plugin` variable with these surfaces:

| Surface | What it does |
|---------|-------------|
| `plugin.runtime` | Inject globals, add methods, eval hooks, import hooks, error hooks |
| `plugin.parser` | Syntax handlers, parser overrides, access to parser primitives |
| `plugin.lexer` | Register keywords, source transforms |
| `plugin.ast` | Constructors for building AST nodes |
| `plugin.meta` | Plugin metadata (name, version, etc.) |
| `plugin.requires(name)` | Declare dependencies on other plugins |
| `plugin.teardown(fn)` | Run cleanup when the interpreter exits |
| `plugin.hooks` | Inspect all currently registered hooks |

---

## Plugin Metadata

Set `plugin.meta` to a map with at least `name` and `version`. This is how the system tracks what's loaded (and how `plugin.requires` works).

```xs
plugin.meta = #{ name: "router", version: "2.1.0" }
```

You can put whatever you want in there -- `description`, `keywords`, `author`, etc. Only `name` is used by the runtime.

---

## Injecting Globals

The most common thing a plugin does. Use `plugin.runtime.global.set(name, value)` to inject a name into the host's global scope.

```xs
plugin.runtime.global.set("clamp", fn(val, lo, hi) {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
})

plugin.runtime.global.set("repeat_str", fn(s, n) {
    var result = ""
    var i = 0
    while i < n { result = result ++ s; i = i + 1 }
    return result
})
```

There's also `plugin.runtime.global.get(name)` to read an existing global, and `plugin.runtime.global.names()` to list all global names.

---

## Adding Methods to Types

Want `.excited()` on every string? `plugin.runtime.add_method(type, name, fn)` does that.

```xs
plugin.runtime.add_method("str", "excited", fn(self) {
    return self ++ "!!!"
})

plugin.runtime.add_method("array", "sum", fn(self) {
    var total = 0
    for x in self { total = total + x }
    return total
})

plugin.runtime.add_method("int", "doubled", fn(self) {
    return self * 2
})
```

```xs
-- in host:
"hello".excited()    -- "hello!!!"
[1, 2, 3].sum()     -- 6
5.doubled()          -- 10
```

The `self` parameter receives the value the method is called on. Type names are: `str`, `int`, `float`, `array`, `map`, `bool`.

---

## Dependencies

If your plugin needs another plugin loaded first:

```xs
plugin.requires("base_framework")
```

This checks if a plugin with that name was already loaded (via its `plugin.meta.name`). If not, the load fails with an error. Order your `use plugin` statements accordingly.

---

## Cleanup

Register teardown callbacks that run when the interpreter exits:

```xs
plugin.teardown(fn() {
    println("plugin shutting down")
})
```

Useful for printing summaries, flushing buffers, closing handles, etc.

---

## Eval Hooks

Hook into expression evaluation. `before_eval` runs before a node is evaluated, `after_eval` runs after.

```xs
-- hook all function calls
let handle = plugin.runtime.before_eval("call", fn(node) {
    println("about to call a function")
    return node  -- must return the node (or a replacement)
})

plugin.runtime.after_eval("call", fn(node, result) {
    println("call returned: {result}")
    return result  -- must return the result (or a replacement)
})
```

The first argument is a tag filter -- it restricts which AST node types trigger the hook. Common tags: `"call"`, `"binop"`, `"ident"`, `"let"`, `"var"`, `"assign"`, `"if"`, `"for"`, `"while"`, `"lambda"`, `"fn"`, `"return"`, `"block"`.

You can also pass just a function with no tag to hook everything (careful -- this is slow):

```xs
plugin.runtime.before_eval(fn(node) {
    return node
})
```

Both functions return a **hook handle** (more on that below).

---

## Hook Handles

Every hook registration returns a handle with a `.remove()` method:

```xs
let trace = plugin.runtime.before_eval("call", fn(node) {
    println("trace: {node}")
    return node
})

-- later, disable it:
trace.remove()
```

This works for `before_eval`, `after_eval`, `on_unknown`, `on_unknown_expr`, `on_postfix`, `resolve_import`, `on_error`, and `transform`. Once removed, the hook stops firing.

---

## Inspecting Hooks

Call `plugin.hooks()` to get a map of all currently registered hooks:

```xs
let h = plugin.hooks()
-- h.before_eval   -> array of callbacks
-- h.after_eval    -> array of callbacks
-- h.on_unknown    -> array of callbacks
-- h.on_postfix    -> array of callbacks
-- h.overrides     -> map of keyword -> callback
-- h.transforms    -> array of callbacks
-- h.resolve_import -> array of callbacks
-- h.on_error      -> array of callbacks
```

Mostly useful for debugging or for meta-plugins that inspect what other plugins have done.

---

## Syntax Extension

This is where plugins get interesting. You can add entirely new syntax to the language.

### Adding Keywords

Before the parser will recognize a new word as a statement, you need to register it:

```xs
plugin.lexer.add_keyword("unless")
```

Without this, the parser treats `unless` as an identifier and you'll get parse errors.

### Handling Unknown Tokens

`plugin.parser.on_unknown(fn)` fires when the parser sees a token it doesn't know how to handle as a statement. Your handler gets the token and returns an AST node (or `null` to pass).

```xs
plugin.parser.on_unknown(fn(token) {
    if token.value == "unless" {
        let cond = plugin.parser.expr()
        let body = plugin.parser.block()
        return plugin.ast.if_expr(
            plugin.ast.unary("!", cond),
            body
        )
    }
    return null
})
```

Now this works in the host:

```xs
unless x > 10 {
    println("x is small")
}
```

The plugin desugars it into `if !(x > 10) { ... }` at parse time.

### Expression-Level Handlers

`plugin.parser.on_unknown_expr(fn)` is the same idea but for expression position. Use this when your custom syntax should be valid inside an expression, not just as a standalone statement.

### Postfix Handlers

`plugin.parser.on_postfix(fn)` fires after an expression is parsed, letting you handle custom postfix operators or suffixes.

### Parser Access

Inside any syntax handler, you have access to parser primitives:

| Method | What it does |
|--------|-------------|
| `plugin.parser.expr()` | Parse and consume one expression |
| `plugin.parser.block()` | Parse and consume a `{ ... }` block |
| `plugin.parser.ident()` | Consume an identifier, return its name as a string |
| `plugin.parser.expect(kind)` | Consume a token of the given kind, error if wrong |
| `plugin.parser.at(kind)` | Check if the current token is of the given kind (no consume) |
| `plugin.parser.peek(offset)` | Look ahead `offset` tokens, returns a token map |

These only work inside parser callbacks (`on_unknown`, `on_unknown_expr`, `on_postfix`, `override`). Calling them outside a parser context returns null.

---

## Parser Override

Override how a built-in keyword is parsed. Your handler receives a `previous` function -- call it to get the default behavior, then transform the result.

```xs
plugin.parser.override("fn", fn(previous) {
    let node = previous()  -- parse fn normally
    -- do something with node, like auto-register it
    if node != null {
        let name = node["name"]
        if name != null && typeof(name) == "str" {
            if name.starts_with("test_") {
                -- auto-register test functions, etc.
            }
        }
    }
    return node
})
```

Keywords you can override: `fn`, `if`, `for`, `while`, `match`, and others. The `previous` function chains -- if two plugins override the same keyword, the second one's `previous` calls the first one's handler.

---

## Lexer Transform

Transform source code before it's parsed. The function receives the raw source string and returns a modified string.

```xs
plugin.lexer.transform(fn(source) {
    -- could do macro expansion, preprocessing, etc.
    return source.replace("MAGIC", "42")
})
```

Use sparingly. This runs on the entire source before parsing, so it's a blunt instrument.

---

## Import Hooks

Intercept `import` statements to provide virtual modules:

```xs
plugin.runtime.resolve_import(fn(name, previous) {
    if name == "server" {
        return #{
            start: fn(port) { println("listening on :{port}") },
            version: "1.0.0"
        }
    }
    -- not ours, delegate to previous handler (or default)
    if previous != null { return previous(name) }
    return null
})
```

```xs
-- in host:
import server
server.start(8080)  -- listening on :8080
```

The `previous` parameter is the previous resolve handler (or null if you're the first). Always delegate to it for names you don't handle.

---

## Error Hooks

Catch unhandled errors:

```xs
plugin.runtime.on_error(fn(error, previous) {
    println("caught error: {error}")
    -- optionally delegate to previous handler
})
```

---

## AST Constructors

When you're building syntax extensions, you need to construct AST nodes. The `plugin.ast` surface gives you constructors for every node type:

### Literals and Identifiers

```xs
plugin.ast.int_node(42)          -- integer literal
plugin.ast.float_node(3.14)      -- float literal
plugin.ast.str_node("hello")     -- string literal
plugin.ast.bool_node(true)       -- boolean literal
plugin.ast.null_node()           -- null literal
plugin.ast.ident("x")           -- identifier reference
```

### Operators

```xs
plugin.ast.binop("+", left, right)   -- binary operation
plugin.ast.unary("!", expr)          -- unary operation
```

### Calls

```xs
plugin.ast.call(func_node, [arg1, arg2])          -- function call
plugin.ast.method_call(obj_node, "method", [args]) -- method call
```

### Control Flow

```xs
plugin.ast.if_expr(cond, then_body)               -- if without else
plugin.ast.if_else(cond, then_body, else_body)     -- if/else
plugin.ast.for_loop("x", iterable, body)           -- for loop
plugin.ast.while_loop(cond, body)                  -- while loop
```

### Declarations

```xs
plugin.ast.let_decl("name", value_node)    -- let binding
plugin.ast.var_decl("name", value_node)    -- var binding
plugin.ast.fn_decl("name", params, body)   -- named function
plugin.ast.lambda(params, body)            -- anonymous function
```

### Structures

```xs
plugin.ast.block([stmt1, stmt2])       -- block of statements
plugin.ast.array([elem1, elem2])       -- array literal
plugin.ast.map([key1, val1, key2, val2]) -- map literal
plugin.ast.return_node(expr)           -- return statement
plugin.ast.assign("name", value_node)  -- assignment
```

### Universal Literals

```xs
plugin.ast.duration(5000)                -- duration node (value in ms)
plugin.ast.color(255, 102, 0, 255)       -- color node (r, g, b, a)
plugin.ast.date("2024-03-15")            -- date node (ISO string)
plugin.ast.size(10240)                   -- size node (value in bytes)
plugin.ast.angle(1.5708)                 -- angle node (value in radians)
```

### Temporal Primitives

```xs
plugin.ast.every(duration_node, body)            -- every loop
plugin.ast.after(duration_node, body)            -- delayed execution
plugin.ast.timeout(duration_node, body, else_body) -- timeout with fallback
plugin.ast.debounce(duration_node, body)         -- debounced execution
```

All of these return map representations of AST nodes that the interpreter knows how to evaluate. You use them inside `on_unknown` / `on_unknown_expr` / `override` handlers to build the desugared form of your custom syntax.

---

## Sandboxing

Don't trust a plugin? Restrict what it can do:

```xs
use plugin "sketchy.xs" sandbox { inject_only }
use plugin "another.xs" sandbox { no_override }
use plugin "strict.xs" sandbox { inject_only, no_override, no_eval_hook }
```

| Flag | Effect |
|------|--------|
| `inject_only` | `global.set` can only create new names, not overwrite existing ones |
| `no_override` | `plugin.parser.override` is disabled |
| `no_eval_hook` | `before_eval` and `after_eval` are disabled |

Combine flags with commas. If a sandboxed plugin tries to do something it's not allowed to, the call silently fails (with a stderr warning) rather than crashing.

---

## Plugin Extensibility

The `plugin` object is a regular map. Plugins can add new surfaces to it, and other plugins can use them. Nothing stops you from building a plugin framework where plugins build on plugins.

---

## Complete Examples

### Simple Utility Plugin

```xs
-- utils_plugin.xs
plugin.meta = #{ name: "utils", version: "1.0.0" }

plugin.runtime.global.set("clamp", fn(val, lo, hi) {
    if val < lo { return lo }
    if val > hi { return hi }
    return val
})

plugin.runtime.global.set("repeat_str", fn(s, n) {
    var result = ""
    var i = 0
    while i < n { result = result ++ s; i = i + 1 }
    return result
})
```

```xs
-- main.xs
use plugin "utils_plugin.xs"
println(clamp(15, 0, 10))       -- 10
println(clamp(-5, 0, 10))       -- 0
println(repeat_str("ha", 3))    -- hahaha
```

### Syntax Sugar Plugin (unless)

```xs
-- unless_plugin.xs
plugin.meta = #{ name: "unless", version: "1.0.0" }

plugin.lexer.add_keyword("unless")

plugin.parser.on_unknown(fn(token) {
    if token.value == "unless" {
        let cond = plugin.parser.expr()
        let body = plugin.parser.block()
        return plugin.ast.if_expr(
            plugin.ast.unary("!", cond),
            body
        )
    }
    return null
})
```

```xs
-- main.xs
use plugin "unless_plugin.xs"

let x = 5
unless x > 10 {
    println("x is not greater than 10")
}
```

### Testing Framework Plugin

```xs
-- tester_plugin.xs
plugin.meta = #{ name: "tester", version: "1.0.0" }

var tests = []
var passed = 0
var failed = 0

plugin.runtime.global.set("test", fn(name, body) {
    tests.push(#{ name: name, body: body })
})

plugin.runtime.global.set("expect_eq", fn(actual, expected) {
    if actual != expected {
        throw "expected {expected}, got {actual}"
    }
})

plugin.runtime.global.set("run_tests", fn() {
    for t in tests {
        try {
            t.body()
            passed = passed + 1
            println("  pass: {t.name}")
        } catch e {
            failed = failed + 1
            println("  FAIL: {t.name} - {e}")
        }
    }
    println("\n{passed} passed, {failed} failed")
})
```

```xs
-- test_math.xs
use plugin "tester_plugin.xs"

test("addition", fn() {
    expect_eq(1 + 1, 2)
    expect_eq(10 + 20, 30)
})

test("strings", fn() {
    expect_eq("hello".len(), 5)
    expect_eq("ab" ++ "cd", "abcd")
})

test("arrays", fn() {
    let a = [1, 2, 3]
    expect_eq(len(a), 3)
})

run_tests()
-- output:
--   pass: addition
--   pass: strings
--   pass: arrays
--
-- 3 passed, 0 failed
```

### Web Framework Plugin (Showcase)

This one uses almost every plugin feature -- custom syntax, virtual modules, eval hooks, method injection, teardown. See `examples/plugins/showcase_plugin.xs` for the full source and `examples/plugin_demo.xs` for usage. Here's the highlights:

```xs
-- showcase_plugin.xs (abbreviated)
plugin.meta = #{ name: "microhttp", version: "0.1.0" }

var routes = []

-- inject framework globals
plugin.runtime.global.set("route", fn(method, path, handler) {
    routes.push(#{ method: method, path: path, handler: handler })
})
plugin.runtime.global.set("response", fn(status, body) {
    return #{ status: status, body: body, headers: #{} }
})

-- custom route syntax: route GET "/path" { ... }
plugin.lexer.add_keyword("route")
plugin.parser.on_unknown(fn(token) {
    if token.value == "route" {
        let method = plugin.parser.ident()
        let path = plugin.parser.expr()
        let body = plugin.parser.block()
        return plugin.ast.call(plugin.ast.ident("route"), [
            plugin.ast.str_node(method), path, plugin.ast.lambda([], body)
        ])
    }
    return null
})

-- virtual module for dispatching
plugin.runtime.resolve_import(fn(name, previous) {
    if name == "server" {
        return #{
            dispatch: fn(method, path) {
                for r in routes {
                    if r.method == method && r.path == path && r.handler != null {
                        return r.handler()
                    }
                }
                return #{ status: 404, body: "not found" }
            }
        }
    }
    if previous != null { return previous(name) }
    return null
})
```

```xs
-- app.xs
use plugin "showcase_plugin.xs"

route GET "/hello" {
    response(200, "Hello, World!")
}

import server
let resp = server.dispatch("GET", "/hello")
println("{resp.status}: {resp.body}")  -- 200: Hello, World!
```

---

## Gotchas

**Sema warnings on plugin-injected names.** If you use a name that a plugin injects (like `greet` or `route`), the static analyzer will warn about an undefined name. This is expected -- sema runs before plugins, so it doesn't know about plugin globals. The warnings are downgraded from errors to warnings for exactly this reason. Your code still runs fine.

**Plugin files run in a temporary interpreter.** The plugin code itself executes in an isolated interpreter. But the hooks it registers (globals, methods, syntax handlers, etc.) affect the host interpreter. Variables defined in the plugin file with `let`/`var` are still accessible from closures the plugin registers -- they live in the plugin's closure environment.

**Load order matters.** Plugins load in the order you write `use plugin` statements. A later plugin can overwrite globals set by an earlier one. If plugin B depends on plugin A, load A first (or use `plugin.requires("A")`).

**If a plugin fails, the host doesn't execute.** Any error during plugin loading -- parse error, runtime error, failed `requires` -- stops everything. You won't get a half-initialized state.

**Syntax extensions trigger a re-parse.** When a plugin registers `on_unknown` handlers or adds keywords, the remaining source gets re-parsed to pick up the new syntax. This is automatic -- you don't need to do anything.

**Hook limits.** There are internal limits on how many hooks you can register (64 eval hooks, 16 syntax handlers, 16 overrides, etc.). You'll get a stderr warning if you hit them. In practice you won't.
