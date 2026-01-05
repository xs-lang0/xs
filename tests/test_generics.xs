-- Test generic type bounds / `where` clauses
--
-- Findings:
--   - Parser fully recognises generic syntax: fn foo<T>(x: T) -> T
--   - Parser stores type_params/type_bounds on fn_decl AST nodes
--   - For structs, enums, classes, traits, and actors generics are parsed
--     but skipped (not stored on the AST node)
--   - `where` clauses are parsed and discarded (both on fn and trait decls)
--   - At runtime, generics are **erased** — the language is dynamically typed,
--     so type parameters have no effect on execution.
--   - Type bounds (e.g. <T: Display>) are parsed but not enforced at runtime.

-- --- Generic function syntax ---
fn identity<T>(x: T) -> T { x }
assert(identity(42) == 42, "generic identity int")
assert(identity("hello") == "hello", "generic identity str")

-- Multiple type parameters
fn pair<A, B>(a: A, b: B) -> [A] { [a, b] }
let result = pair(1, "two")
assert(result[0] == 1, "generic pair first")
assert(result[1] == "two", "generic pair second")

-- Type bounds (parsed but erased at runtime)
fn bounded<T: Display>(x: T) -> T { x }
assert(bounded(99) == 99, "bounded generic")

-- Where clause (parsed and discarded)
fn with_where<T>(x: T) -> T where T: Display { x }
assert(with_where("ok") == "ok", "where clause fn")

-- --- Generic struct ---
struct Pair<A, B> { first, second }
let p = Pair { first: 1, second: "two" }
assert(p.first == 1, "generic struct field")
assert(p.second == "two", "generic struct field 2")

-- --- Generic class ---
class Box<T> {
    fn init(self, val) { self.val = val }
    fn get(self) { self.val }
}
let b = Box(42)
assert(b.get() == 42, "generic class")

-- --- Generic enum (parsing only — skip generics) ---
enum Option<T> {
    Some,
    None
}

println("All generics tests passed!")
