-- r"..." raw strings
let raw = r"\n\t{not_interpolated}"
println(raw)

-- r"""...""" raw multiline
let raw_multi = r"""
{literal}
\n
"""
println(raw_multi)

-- Triple-quote dedent
let dedented = """
    line one
    line two
    """
println(dedented)

-- contains builtin
let has = contains("hello world", "world")
println(has)
let not_has = contains("hello world", "xyz")
println(not_has)

println("strings 2a tests passed")
