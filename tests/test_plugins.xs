-- plugin system tests

-- test basic plugin injection
use plugin "plugins/greet_plugin.xs"
assert_eq(greet("World"), "Hello, World!")
assert_eq(greet("XS"), "Hello, XS!")
assert_eq(shout("hello"), "HELLO!!!")
assert_eq(shout("test"), "TEST!!!")

-- test add_method
use plugin "plugins/method_plugin.xs"
assert_eq("hello".excited(), "hello!!!")
assert_eq("test".excited(), "test!!!")
assert_eq([1, 2, 3].sum(), 6)
assert_eq([10, 20, 30].sum(), 60)
assert_eq([].sum(), 0)
assert_eq(5.doubled(), 10)
assert_eq(0.doubled(), 0)
