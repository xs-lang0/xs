-- tests/test_io_module.xs — exercise IO module functions using temp files
import io
import json

-- Helper: generate a unique temp file path
let tmp = io.temp_file()
assert(tmp != null, "temp_file returns a path")
assert(type(tmp) == "str", "temp_file returns string")

-- Clean up the auto-created temp file so we can use the path fresh
io.delete_file(tmp)

-- ============================================================
-- 1. write_file / read_file
-- ============================================================
let content = "hello, io module!\nline two\n"
assert(io.write_file(tmp, content) == true, "write_file succeeds")
let read_back = io.read_file(tmp)
assert(read_back == content, "read_file returns exact content")

-- ============================================================
-- 2. file_exists / exists alias
-- ============================================================
assert(io.file_exists(tmp) == true, "file_exists on existing file")
assert(io.exists(tmp) == true, "exists alias works")
assert(io.file_exists(tmp + ".nonexistent") == false, "file_exists on missing file")

-- ============================================================
-- 3. file_size / size alias
-- ============================================================
let sz = io.file_size(tmp)
assert(sz == len(content), "file_size matches content length")
assert(io.size(tmp) == sz, "size alias works")

-- ============================================================
-- 4. is_file / is_dir
-- ============================================================
assert(io.is_file(tmp) == true, "is_file on a file")
assert(io.is_dir(tmp) == false, "is_dir on a file returns false")
assert(io.is_dir("/tmp") == true, "is_dir on /tmp")
assert(io.is_file("/tmp") == false, "is_file on /tmp returns false")

-- ============================================================
-- 5. append_file
-- ============================================================
assert(io.append_file(tmp, "appended\n") == true, "append_file succeeds")
let appended = io.read_file(tmp)
assert(appended == content + "appended\n", "append_file adds to end")

-- ============================================================
-- 6. copy_file
-- ============================================================
let tmp_copy = tmp + ".copy"
assert(io.copy_file(tmp, tmp_copy) == true, "copy_file succeeds")
assert(io.read_file(tmp_copy) == io.read_file(tmp), "copy_file content matches")
io.delete_file(tmp_copy)

-- ============================================================
-- 7. rename_file
-- ============================================================
let tmp_renamed = tmp + ".renamed"
io.write_file(tmp_renamed, "to rename")
let tmp_new = tmp + ".newname"
assert(io.rename_file(tmp_renamed, tmp_new) == true, "rename_file succeeds")
assert(io.file_exists(tmp_renamed) == false, "old name gone after rename")
assert(io.read_file(tmp_new) == "to rename", "renamed file has correct content")
io.delete_file(tmp_new)

-- ============================================================
-- 8. move_file
-- ============================================================
let tmp_src = tmp + ".src"
let tmp_dst = tmp + ".dst"
io.write_file(tmp_src, "move me")
assert(io.move_file(tmp_src, tmp_dst) == true, "move_file succeeds")
assert(io.file_exists(tmp_src) == false, "source gone after move")
assert(io.read_file(tmp_dst) == "move me", "moved file content correct")
io.delete_file(tmp_dst)

-- ============================================================
-- 9. delete_file
-- ============================================================
let tmp_del = tmp + ".del"
io.write_file(tmp_del, "delete me")
assert(io.delete_file(tmp_del) == true, "delete_file succeeds")
assert(io.file_exists(tmp_del) == false, "file gone after delete")

-- ============================================================
-- 10. read_lines / write_lines
-- ============================================================
let lines = ["alpha", "beta", "gamma"]
assert(io.write_lines(tmp, lines) == true, "write_lines succeeds")
let read_lines = io.read_lines(tmp)
assert(len(read_lines) == 3, "read_lines returns 3 lines")
assert(read_lines[0] == "alpha", "read_lines first line")
assert(read_lines[1] == "beta", "read_lines second line")
assert(read_lines[2] == "gamma", "read_lines third line")

-- ============================================================
-- 11. read_bytes / write_bytes
-- ============================================================
let bytes = [72, 101, 108, 108, 111]  -- "Hello"
assert(io.write_bytes(tmp, bytes) == true, "write_bytes succeeds")
let rb = io.read_bytes(tmp)
assert(len(rb) == 5, "read_bytes length")
assert(rb[0] == 72, "read_bytes first byte H")
assert(rb[4] == 111, "read_bytes last byte o")

-- ============================================================
-- 12. make_dir / list_dir
-- ============================================================
let tmp_dir = tmp + "_dir"
assert(io.make_dir(tmp_dir) == true, "make_dir succeeds")
assert(io.is_dir(tmp_dir) == true, "make_dir creates directory")

-- Create some files inside
io.write_file(tmp_dir + "/a.txt", "a")
io.write_file(tmp_dir + "/b.txt", "b")
let listing = io.list_dir(tmp_dir)
assert(len(listing) == 2, "list_dir returns 2 entries")

-- Clean up dir contents
io.delete_file(tmp_dir + "/a.txt")
io.delete_file(tmp_dir + "/b.txt")

-- ============================================================
-- 13. file_info
-- ============================================================
io.write_file(tmp, "info test data")
let info = io.file_info(tmp)
assert(info != null, "file_info returns non-null")
assert(info.size == 14, "file_info size is correct")
assert(info.is_file == true, "file_info is_file")
assert(info.is_dir == false, "file_info is_dir")
assert(info.path == tmp, "file_info path")
assert(info.modified > 0, "file_info modified timestamp")

-- ============================================================
-- 14. temp_file
-- ============================================================
let tf = io.temp_file()
assert(tf != null, "temp_file creates file")
assert(io.file_exists(tf) == true, "temp_file created on disk")
io.delete_file(tf)

-- ============================================================
-- 15. temp_dir
-- ============================================================
let td = io.temp_dir()
assert(td != null, "temp_dir creates dir")
assert(io.is_dir(td) == true, "temp_dir is a directory")

-- ============================================================
-- 16. read_json / write_json
-- ============================================================
let data = #{name: "xs", version: 1, tags: ["fast", "fun"]}
assert(io.write_json(tmp, data) == true, "write_json succeeds")
let parsed = io.read_json(tmp)
assert(parsed != null, "read_json returns non-null")
assert(parsed.name == "xs", "read_json name field")
assert(parsed.version == 1, "read_json version field")
assert(len(parsed.tags) == 2, "read_json tags array length")
assert(parsed.tags[0] == "fast", "read_json tags[0]")

-- ============================================================
-- 17. glob
-- ============================================================
io.write_file(tmp_dir + "/x.txt", "x")
io.write_file(tmp_dir + "/y.txt", "y")
let matches = io.glob(tmp_dir + "/*.txt")
assert(len(matches) == 2, "glob finds 2 txt files")
io.delete_file(tmp_dir + "/x.txt")
io.delete_file(tmp_dir + "/y.txt")

-- ============================================================
-- 18. stdout/stderr sub-modules (just check they are callable)
-- ============================================================
io.stdout.write("stdout_write_test ")
io.stdout.writeln("ok")
io.stdout.flush()
io.stderr.write("")
io.stderr.flush()

-- ============================================================
-- Cleanup
-- ============================================================
io.delete_file(tmp)

println("All IO module tests passed!")
