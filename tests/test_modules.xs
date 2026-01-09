-- module imports, stdlib modules

-- math module
import math
assert_eq(type(math.PI), "float")
assert(math.PI > 3.14, "PI value")
assert(math.PI < 3.15, "PI value upper")
assert_eq(math.abs(-5), 5)
assert_eq(math.floor(3.7), 3)
assert_eq(math.ceil(3.2), 4)
assert_eq(math.round(3.5), 4)
assert(math.sqrt(4.0) == 2.0, "sqrt 4")

-- string module
import string
assert_eq(type(string), "module")

-- time module
import time
assert_eq(type(time), "module")

-- module fields are accessible
assert(math.E > 2.71, "E value")
assert(math.E < 2.72, "E upper")

-- math.is_nan and math.is_inf
assert(math.is_nan(math.nan), "is_nan")
assert(math.is_inf(math.inf), "is_inf")
assert(!math.is_nan(1.0), "not nan")
assert(!math.is_inf(1.0), "not inf")

println("test_modules: all passed")
