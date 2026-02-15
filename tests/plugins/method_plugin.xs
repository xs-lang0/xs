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
