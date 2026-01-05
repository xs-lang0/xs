-- =============================================================
-- Module/Import System Verification
-- =============================================================
--
-- FINDINGS (from source analysis):
--
-- 1. BUILT-IN MODULE IMPORTS: Fully supported.
--    Syntax: `import math`, `import string`, `import io`, etc.
--    The interpreter pre-registers ~30 stdlib modules (math, string,
--    io, time, path, json, os, collections, random, etc.) as globals.
--    `import X` simply looks up X in the environment and re-binds it.
--
-- 2. FROM-IMPORT SYNTAX: Supported for extracting items from modules.
--    Syntax: `from math import { sqrt, sin }`
--    This pulls specific names out of a module map into the local env.
--
-- 3. ALIAS IMPORTS: Supported.
--    Syntax: `import math as m`
--    Binds the module under a different name.
--
-- 4. INLINE MODULE DECLARATIONS: Supported.
--    Syntax: `module mymod { fn foo() { ... } let x = 1 }`
--    Executes the body in a sub-environment, collects all bindings
--    into an XS_MODULE value.
--
-- 5. FILE-BASED IMPORTS: NOT currently supported.
--    The import system only resolves names against the global env
--    (pre-registered stdlib modules). There is no file-loading,
--    path resolution, or module caching for .xs source files.
--    `import "test_import_file_helper"` will fail silently.
--
-- 6. EXPORT KEYWORD: Exists as a token (TK_EXPORT) and is lexed,
--    but the parser treats it as an identifier (falls through to
--    NODE_IDENT). It has no semantic effect — there is no export
--    mechanism. In inline modules, all definitions are exported.
--
-- 7. RELATIVE IMPORTS: Not supported (no file imports at all).
--
-- 8. DOTTED/NAMESPACED PATHS: The parser supports `import a.b.c`
--    or `import a::b::c` (multi-part paths), but the interpreter
--    only looks at the first path component (path[0]) as the module
--    name in the environment.
-- =============================================================

-- --- Test 1: Basic module import ---
import math
let sq = math.sqrt(16)
assert(sq == 4.0, "math.sqrt(16) should be 4.0")

-- --- Test 2: Module member access (constants are uppercase) ---
let pi_val = math.PI
assert(pi_val > 3.14, "math.PI should be > 3.14")
assert(pi_val < 3.15, "math.PI should be < 3.15")

-- --- Test 3: String module import ---
import string
let lev = string.levenshtein("cat", "car")
assert(lev == 1, "levenshtein('cat','car') should be 1")

-- --- Test 4: from-import syntax (extract specific items) ---
from math import { sqrt }
let sq2 = sqrt(25)
assert(sq2 == 5.0, "sqrt(25) from math should be 5.0")

-- --- Test 5: Alias import ---
import math as m
let sq3 = m.sqrt(9)
assert(sq3 == 3.0, "m.sqrt(9) with alias should be 3.0")

-- --- Test 6: Inline module declaration ---
module mymod {
    fn greet(name) { "hello " + name }
    let VERSION = 1
}
assert(mymod.greet("xs") == "hello xs", "inline module function")
assert(mymod.VERSION == 1, "inline module constant")

-- --- Test 7: io module import ---
import io

-- --- Test 8: json module import ---
import json

-- --- Test 9: Multiple module imports ---
import os
import path
import collections

println("Import system test passed!")
