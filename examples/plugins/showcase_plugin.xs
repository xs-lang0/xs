-- showcase_plugin.xs: micro web framework demonstrating every plugin system feature

-- 1. plugin.meta
plugin.meta = #{
    name: "microhttp", version: "0.1.0",
    description: "micro web framework with route DSL, JSON helpers, and request tracing",
    keywords: ["route", "GET", "POST"]
}

-- 2. plugin.requires (no deps, but show the pattern)
-- plugin.requires("some_base_plugin", ">=1.0")

-- internal state
var routes = []
var call_count = 0
var fn_parse_count = 0
var debug_mode = false

-- 3. plugin.runtime.global.set — inject framework globals
plugin.runtime.global.set("route", fn(method, path, handler) {
    routes.push(#{ method: method, path: path, handler: handler })
})
let make_response = fn(status, body) {
    return #{ status: status, body: body, headers: #{} }
}
plugin.runtime.global.set("response", make_response)
plugin.runtime.global.set("json_response", fn(data) {
    return #{ status: 200, body: data, headers: #{"content-type": "application/json"} }
})
plugin.runtime.global.set("log", fn(level, msg) {
    let color = if level == "info" { "\e[32m" }
                elif level == "warn" { "\e[33m" }
                elif level == "error" { "\e[31m" }
                else { "\e[36m" }
    println("{color}[{level}]\e[0m {msg}")
})
plugin.runtime.global.set("set_debug", fn(on) { debug_mode = on })

-- 4. plugin.runtime.add_method — JSON serialization on built-in types
plugin.runtime.add_method("str", "to_json", fn(self) {
    return "\"" ++ self ++ "\""
})
plugin.runtime.add_method("map", "to_json", fn(self) {
    var parts = []
    for k in self.keys() {
        let v = self[k]
        let vs = if typeof(v) == "str" { "\"" ++ v ++ "\"" } else { "{v}" }
        parts.push("\"" ++ k ++ "\": " ++ vs)
    }
    return "\{" ++ parts.join(", ") ++ "\}"
})
plugin.runtime.add_method("array", "to_json", fn(self) {
    var parts = []
    for item in self {
        let s = if typeof(item) == "str" { "\"" ++ item ++ "\"" } else { "{item}" }
        parts.push(s)
    }
    return "[" ++ parts.join(", ") ++ "]"
})

-- 5. plugin.runtime.before_eval — debug tracing (13. hook .remove() demo)
let trace_hook = plugin.runtime.before_eval("call", fn(node) {
    if debug_mode { println("\e[90m  trace: call\e[0m") }
    return node
})

-- 6. plugin.runtime.after_eval — count function calls for metrics
plugin.runtime.after_eval("call", fn(node, result) {
    call_count = call_count + 1
    return result
})

-- 7. plugin.lexer.add_keyword
plugin.lexer.add_keyword("route")

-- 8. plugin.parser.on_unknown + 14. plugin.ast constructors + 15. plugin.parser.expr/block
plugin.parser.on_unknown(fn(token) {
    if token.value == "route" {
        let method = plugin.parser.ident()       -- consume method name
        let path_node = plugin.parser.expr()     -- parse path string
        let body = plugin.parser.block()         -- parse handler block
        -- desugar: route GET "/hello" { ... } => route("GET", "/hello", fn() { ... })
        return plugin.ast.call(plugin.ast.ident("route"), [
            plugin.ast.str_node(method), path_node, plugin.ast.lambda([], body)
        ])
    }
    return null
})

-- 9. plugin.parser.override — auto-register handle_* functions as routes
plugin.parser.override("fn", fn(previous) {
    let node = previous()
    fn_parse_count = fn_parse_count + 1
    if node != null {
        let name_val = node["name"]
        if name_val != null && typeof(name_val) == "str" {
            if name_val.starts_with("handle_") {
                routes.push(#{ method: "GET", path: "/" ++ name_val.slice(7), handler: null })
            }
        }
    }
    return node
})

-- 10. plugin.runtime.resolve_import — virtual `server` module
plugin.runtime.resolve_import(fn(name, previous) {
    if name == "server" {
        return #{
            start: fn(port) {
                log("info", "server listening on :{port}")
                for r in routes { log("info", "  {r.method} {r.path}") }
            },
            routes: fn() {
                var names = []
                for r in routes { names.push("{r.method} {r.path}") }
                return names
            },
            metrics: fn() {
                return #{ total_calls: call_count, route_count: len(routes), fn_decls: fn_parse_count }
            },
            dispatch: fn(method, path) {
                for r in routes {
                    if r.method == method && r.path == path && r.handler != null {
                        return r.handler()
                    }
                }
                return make_response(404, "not found")
            }
        }
    }
    if previous != null { return previous(name) }
    return null
})

-- 11. plugin.runtime.on_error — catch unhandled errors gracefully
plugin.runtime.on_error(fn(error, previous) {
    log("error", "caught: {error}")
})

-- 12. plugin.teardown — print summary on exit
plugin.teardown(fn() {
    println("\n--- microhttp summary ---")
    println("routes registered: {len(routes)}")
    for r in routes { println("  {r.method} {r.path}") }
    println("total function calls: {call_count}")
    println("fn declarations parsed: {fn_parse_count}")
})
