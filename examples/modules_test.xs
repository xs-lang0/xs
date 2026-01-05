// XS Language -- Stdlib Modules Test
// Tests: string, hash, base64, uuid, time, process, collections, math

import string
import hash
import base64
import uuid
import time
import process
import collections
import math

fn main() {
    // -- string module --
    println("=== string ===")
    let hi = "hi"
    let hw = "hello world"
    let camel = "myFunctionName"
    let snake = "my_function_name"
    let html_src = "<b>bold</b>"
    let sentence = "  hello   world  "
    let num_str = "3.14"
    println("pad_left:       '{string.pad_left(hi, 8)}'")
    println("pad_right:      '{string.pad_right(hi, 8)}'")
    println("center:         '{string.center(hi, 8)}'")
    println("truncate:       '{string.truncate(hw, 8)}'")
    println("similarity:     {string.similarity("kitten", "sitting")}")
    println("levenshtein:    {string.levenshtein("kitten", "sitting")}")
    println("camel_to_snake: {string.camel_to_snake(camel)}")
    println("snake_to_camel: {string.snake_to_camel(snake)}")
    println("escape_html:    {string.escape_html(html_src)}")
    println("words:          {string.words(sentence)}")
    println("is_numeric:     {string.is_numeric(num_str)}")

    // -- math module --
    println("")
    println("=== math ===")
    println("sqrt(16):  {math.sqrt(16)}")
    println("floor(3.7): {math.floor(3.7)}")
    println("ceil(3.2):  {math.ceil(3.2)}")
    println("sin(0):     {math.sin(0)}")
    println("cos(0):     {math.cos(0)}")
    println("pow(2, 10): {math.pow(2, 10)}")
    println("log(1):     {math.log(1)}")

    // -- hash module --
    println("")
    println("=== hash ===")
    let hello = "hello"
    println("md5:    {hash.md5(hello)}")
    println("sha256: {hash.sha256(hello)}")

    // -- base64 module --
    println("")
    println("=== base64 ===")
    let b64_src = "Hello, XS!"
    let enc = base64.encode(b64_src)
    println("encode: {enc}")
    println("decode: {base64.decode(enc)}")

    // -- uuid module --
    println("")
    println("=== uuid ===")
    let id = uuid.v4()
    let bad_id = "not-a-uuid"
    println("v4:            {id}")
    println("is_valid(v4):  {uuid.is_valid(id)}")
    println("is_valid(bad): {uuid.is_valid(bad_id)}")

    // -- time module --
    println("")
    println("=== time ===")
    println("now:    {time.now()}")
    let sw = time.stopwatch()
    println("elapsed: {sw.elapsed_ms()} ms")

    // -- process module --
    println("")
    println("=== process ===")
    println("pid: {process.pid()}")
    let r = process.run("echo hello from process")
    println("run ok: {r.ok}  stdout: '{r.stdout}'")

    // -- collections module --
    println("")
    println("=== collections ===")
    let stack = collections.Stack()
    stack.push(1)
    stack.push(2)
    stack.push(3)
    println("stack peek: {stack.peek()}  len: {stack.len()}")
    println("stack pop:  {stack.pop()}")

    let pq = collections.PriorityQueue()
    pq.push("low", 1)
    pq.push("high", 10)
    pq.push("medium", 5)
    println("pq pop (high):   {pq.pop()}")
    println("pq pop (medium): {pq.pop()}")

    let nums = [1, 2, 2, 3, 3, 3]
    let counter = collections.Counter(nums)
    println("counter get(3):      {counter.get(3)}")
    println("counter most_common: {counter.most_common(2)}")

    println("")
    println("All module tests complete.")
}
