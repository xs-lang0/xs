import math as m
assert(m.sqrt(4) == 2.0, "math.sqrt via alias")
assert(m.PI > 3.14, "math.PI via alias")

import string as str_lib
let r = str_lib.pad_left("hi", 5, " ")
assert(r == "   hi", "string.pad_left via alias")

println("All import alias tests passed!")
