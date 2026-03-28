-- plugin_demo.xs: exercise every feature of the showcase plugin
use plugin "plugins/showcase_plugin.xs"

-- custom route syntax (plugin.lexer.add_keyword + plugin.parser.on_unknown)
route GET "/hello" {
    response(200, "Hello, World!")
}

route POST "/echo" {
    response(200, "echoed")
}

-- injected globals (plugin.runtime.global.set)
log("info", "setting up routes")

-- auto-registered function via fn override (plugin.parser.override)
fn handle_status() {
    return response(200, "ok")
}

-- virtual module (plugin.runtime.resolve_import)
import server
println("registered routes: {server.routes()}")
println("metrics: {server.metrics()}")

-- dispatch a request through the router
let resp = server.dispatch("GET", "/hello")
println("GET /hello -> {resp.status}: {resp.body}")

let resp2 = server.dispatch("POST", "/echo")
println("POST /echo -> {resp2.status}: {resp2.body}")

let resp404 = server.dispatch("GET", "/nope")
println("GET /nope -> {resp404.status}: {resp404.body}")

-- type methods (plugin.runtime.add_method)
-- (the plugin registers to_json on str/map/array but method dispatch
-- for plugin-injected methods on built-in types is limited)

-- framework complete
log("info", "demo complete")
