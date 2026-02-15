plugin.meta = #{ name: "greet", version: "1.0.0" }

plugin.runtime.global.set("greet", fn(name) {
    return "Hello, {name}!"
})

plugin.runtime.global.set("shout", fn(msg) {
    return msg.upper() ++ "!!!"
})
