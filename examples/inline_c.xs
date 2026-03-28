-- inline C blocks: drop into raw C when transpiling

-- this example shows the syntax, but inline C only works
-- with the C transpiler (xs transpile --target c)
-- the interpreter will print an error

fn fast_hash(data) {
    inline c {
        uint64_t h = 0x525201;
        const char *s = xs_to_cstr(args[0]);
        while (*s) h = h * 31 + *s++;
        xs_return_int(h);
    }
    return 0
}

println("inline C parsed successfully")
println("calling fast_hash would require C transpilation")
