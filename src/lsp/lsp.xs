import io
import json
import process
import os

var XS_BIN = "xs"
let TMP_CHECK = io.temp_file(".xs")
let TMP_FMT = io.temp_file(".xs")

-- document store: uri -> #{"text": ..., "version": ...}
var documents = #{}

fn doc_open(uri, text, version) {
    documents[uri] = #{"text": text, "version": version}
}

fn doc_update(uri, text, version) {
    documents[uri] = #{"text": text, "version": version}
}

fn doc_close(uri) {
    documents.delete(uri)
}

fn doc_get(uri) {
    return documents.get(uri, null)
}

fn doc_text(uri) {
    let doc = doc_get(uri)
    if doc == null { return "" }
    return doc["text"]
}

-- protocol layer

fn read_message() {
    var content_length = -1
    loop {
        let line = io.stdin_readline()
        if line == null { return null }
        let trimmed = line.trim()
        if trimmed == "" {
            if content_length > 0 {
                let body = io.stdin_read_n(content_length)
                return json.parse(body)
            }
            return null
        }
        if trimmed.starts_with("Content-Length:") {
            let val = trimmed.slice(15, trimmed.len()).trim()
            content_length = val.parse_int()
        }
    }
}

fn send_raw(obj) {
    let body = json.stringify(obj)
    let header = "Content-Length: {body.len()}\r\n\r\n"
    io.stdout.write(header)
    io.stdout.write(body)
    io.stdout.flush()
}

fn send_response(id, result) {
    send_raw(#{"jsonrpc": "2.0", "id": id, "result": result})
}

fn send_error(id, code, msg) {
    send_raw(#{
        "jsonrpc": "2.0",
        "id": id,
        "error": #{"code": code, "message": msg}
    })
}

fn send_notification(method, params) {
    send_raw(#{"jsonrpc": "2.0", "method": method, "params": params})
}

fn log(msg) {
    io.stderr.writeln("[xs-lsp] {msg}")
}

-- uri/path helpers

fn uri_to_path(uri) {
    if uri.starts_with("file:///") {
        let rest = uri.slice(7, uri.len())
        -- windows: file:///C:/foo -> C:/foo
        if rest.len() > 2 and rest.slice(1, 2) == ":" {
            return rest
        }
        -- unix: file:///home/foo -> /home/foo
        return rest
    }
    return uri
}

fn path_to_uri(p) {
    if p.starts_with("/") {
        return "file://{p}"
    }
    return "file:///{p}"
}

-- text utilities

fn get_line(text, line_num) {
    let lines = text.split("\n")
    if line_num < 0 or line_num >= lines.len() { return "" }
    return lines[line_num]
}

fn is_ident_char(ch) {
    if ch >= "a" and ch <= "z" { return true }
    if ch >= "A" and ch <= "Z" { return true }
    if ch >= "0" and ch <= "9" { return true }
    if ch == "_" { return true }
    return false
}

fn get_word_at(text, line, col) {
    let line_text = get_line(text, line)
    if line_text == "" { return "" }
    var c = col
    if c >= line_text.len() { c = line_text.len() - 1 }
    if c < 0 { return "" }

    var start = c
    while start > 0 and is_ident_char(line_text.slice(start - 1, start)) {
        start = start - 1
    }
    var end = c
    while end < line_text.len() and is_ident_char(line_text.slice(end, end + 1)) {
        end = end + 1
    }
    if start == end { return "" }
    return line_text.slice(start, end)
}

fn find_whole_word(line_text, word, from_pos) {
    var pos = from_pos
    while pos <= line_text.len() - word.len() {
        let idx = line_text.slice(pos, line_text.len()).index_of(word)
        if idx < 0 { return -1 }
        let abs_pos = pos + idx
        let before_ok = abs_pos == 0 or not is_ident_char(line_text.slice(abs_pos - 1, abs_pos))
        let after_pos = abs_pos + word.len()
        let after_ok = after_pos >= line_text.len() or not is_ident_char(line_text.slice(after_pos, after_pos + 1))
        if before_ok and after_ok { return abs_pos }
        pos = abs_pos + 1
    }
    return -1
}

-- strip ANSI escape codes by checking byte values
fn strip_ansi(text) {
    let raw = text.bytes()
    var clean_bytes = []
    var i = 0
    while i < raw.len() {
        if raw[i] == 27 {
            i = i + 1
            if i < raw.len() and raw[i] == 91 {
                i = i + 1
                while i < raw.len() {
                    let b = raw[i]
                    i = i + 1
                    if (b >= 65 and b <= 90) or (b >= 97 and b <= 122) { break }
                }
            }
        } else {
            clean_bytes.push(raw[i])
            i = i + 1
        }
    }
    var result = ""
    for b in clean_bytes {
        result = result + char(b)
    }
    return result
}

-- diagnostics via xs --check

fn run_diagnostics(uri) {
    let text = doc_text(uri)
    if text == "" {
        send_notification("textDocument/publishDiagnostics", #{
            "uri": uri, "diagnostics": []
        })
        return
    }

    io.write_file(TMP_CHECK, text)
    log("checking {TMP_CHECK} with {XS_BIN}")
    let res = process.run("{XS_BIN} --no-color --check {TMP_CHECK} 2>&1")
    let output = strip_ansi(res["stdout"])
    log("check result: code={res["code"]} output_len={output.len()}")
    if output.len() > 0 { log("output: {output.slice(0, if output.len() > 200 { 200 } else { output.len() })}") }
    var diags = []

    let lines = output.split("\n")
    var i = 0
    while i < lines.len() {
        let line = lines[i]
        if line.starts_with("error") or line.starts_with("warning") {
            let severity = if line.starts_with("error") { 1 } else { 2 }
            -- extract message after first ]:
            var msg = line
            let bracket_idx = line.index_of("]: ")
            if bracket_idx >= 0 {
                msg = line.slice(bracket_idx + 3, line.len())
            }

            -- look for --> file:LINE:COL on next line
            var diag_line = 0
            var diag_col = 0
            if i + 1 < lines.len() {
                let next = lines[i + 1].trim()
                if next.starts_with("-->") {
                    let arrow_part = next.slice(4, next.len())
                    let parts = arrow_part.split(":")
                    if parts.len() >= 3 {
                        diag_line = parts[parts.len() - 2].trim().parse_int() - 1
                        diag_col = parts[parts.len() - 1].trim().parse_int() - 1
                        if diag_line < 0 { diag_line = 0 }
                        if diag_col < 0 { diag_col = 0 }
                    }
                    i = i + 1
                }
            }

            diags.push(#{
                "range": #{
                    "start": #{"line": diag_line, "character": diag_col},
                    "end": #{"line": diag_line, "character": diag_col + 1}
                },
                "severity": severity,
                "source": "xs",
                "message": msg
            })
        }
        i = i + 1
    }

    try { io.delete_file(TMP_CHECK) } catch e {}

    send_notification("textDocument/publishDiagnostics", #{
        "uri": uri, "diagnostics": diags
    })
}

-- symbol extraction

let SYM_MODULE = 2
let SYM_CLASS = 5
let SYM_ENUM = 10
let SYM_INTERFACE = 11
let SYM_FUNCTION = 12
let SYM_VARIABLE = 13
let SYM_CONSTANT = 14
let SYM_STRUCT = 23

fn extract_symbols(text) {
    let lines = text.split("\n")
    var symbols = []
    var i = 0
    while i < lines.len() {
        let line = lines[i].trim()

        if line.starts_with("fn ") {
            let rest = line.slice(3, line.len())
            let paren = rest.index_of("(")
            if paren > 0 {
                let name = rest.slice(0, paren).trim()
                let params = rest.slice(paren, rest.len())
                let close = params.index_of(")")
                let detail = if close >= 0 { "fn {name}{params.slice(0, close + 1)}" } else { "fn {name}" }
                symbols.push(#{"name": name, "kind": SYM_FUNCTION, "line": i, "col": 0, "detail": detail})
            }
        } elif line.starts_with("let ") {
            let rest = line.slice(4, line.len())
            let eq = rest.index_of("=")
            let name = if eq > 0 { rest.slice(0, eq).trim() } else { rest.trim() }
            -- strip type annotation
            let colon = name.index_of(":")
            let clean_name = if colon > 0 { name.slice(0, colon).trim() } else { name }
            symbols.push(#{"name": clean_name, "kind": SYM_VARIABLE, "line": i, "col": 0, "detail": "let"})
        } elif line.starts_with("var ") {
            let rest = line.slice(4, line.len())
            let eq = rest.index_of("=")
            let name = if eq > 0 { rest.slice(0, eq).trim() } else { rest.trim() }
            let colon = name.index_of(":")
            let clean_name = if colon > 0 { name.slice(0, colon).trim() } else { name }
            symbols.push(#{"name": clean_name, "kind": SYM_VARIABLE, "line": i, "col": 0, "detail": "var"})
        } elif line.starts_with("const ") {
            let rest = line.slice(6, line.len())
            let eq = rest.index_of("=")
            let name = if eq > 0 { rest.slice(0, eq).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_CONSTANT, "line": i, "col": 0, "detail": "const"})
        } elif line.starts_with("struct ") {
            let rest = line.slice(7, line.len())
            let brace = rest.index_of("\{")
            let name = if brace > 0 { rest.slice(0, brace).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_STRUCT, "line": i, "col": 0, "detail": "struct"})
        } elif line.starts_with("enum ") {
            let rest = line.slice(5, line.len())
            let brace = rest.index_of("\{")
            let name = if brace > 0 { rest.slice(0, brace).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_ENUM, "line": i, "col": 0, "detail": "enum"})
        } elif line.starts_with("trait ") {
            let rest = line.slice(6, line.len())
            let brace = rest.index_of("\{")
            let name = if brace > 0 { rest.slice(0, brace).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_INTERFACE, "line": i, "col": 0, "detail": "trait"})
        } elif line.starts_with("class ") {
            let rest = line.slice(6, line.len())
            let brace = rest.index_of("\{")
            let name = if brace > 0 { rest.slice(0, brace).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_CLASS, "line": i, "col": 0, "detail": "class"})
        } elif line.starts_with("module ") {
            let rest = line.slice(7, line.len())
            let brace = rest.index_of("\{")
            let name = if brace > 0 { rest.slice(0, brace).trim() } else { rest.trim() }
            symbols.push(#{"name": name, "kind": SYM_MODULE, "line": i, "col": 0, "detail": "module"})
        } elif line.starts_with("import ") {
            let name = line.slice(7, line.len()).trim()
            symbols.push(#{"name": name, "kind": SYM_MODULE, "line": i, "col": 0, "detail": "import"})
        }

        i = i + 1
    }
    return symbols
}

-- known lists

let KEYWORDS = [
    "fn", "let", "var", "const", "if", "else", "elif", "while", "for", "in",
    "loop", "match", "when", "struct", "enum", "trait", "impl", "import", "export",
    "return", "break", "continue", "true", "false", "null", "and", "or", "not",
    "is", "try", "catch", "finally", "throw", "defer", "yield", "async", "await",
    "pub", "mut", "static", "self", "super", "module", "class", "type", "as",
    "from", "effect", "perform", "handle", "resume", "spawn", "nursery", "macro", "use"
]

let BUILTINS = [
    "print", "println", "input", "len", "type", "range", "keys", "values",
    "entries", "map", "filter", "reduce", "zip", "any", "all", "min", "max",
    "sum", "sort", "reverse", "push", "pop", "slice", "join", "split",
    "contains", "starts_with", "ends_with", "trim", "upper", "lower", "replace",
    "typeof", "assert", "panic"
]

let TYPES = [
    "int", "float", "str", "bool", "char", "array", "map", "tuple", "any", "void"
]

let MODULES = [
    "math", "time", "string", "path", "base64", "hash", "uuid", "collections",
    "random", "json", "log", "fmt", "csv", "url", "re", "process", "io",
    "async", "net", "crypto", "thread", "buf", "encode", "db", "cli", "ffi",
    "reflect", "gc", "reactive", "os"
]

let STRING_METHODS = [
    "len", "upper", "lower", "trim", "contains", "starts_with", "ends_with",
    "split", "replace", "chars", "bytes", "repeat", "join", "slice", "index_of",
    "parse_int", "parse_float", "is_empty", "reverse", "capitalize",
    "pad_left", "pad_right", "count", "find", "rfind", "title"
]

let ARRAY_METHODS = [
    "len", "push", "pop", "first", "last", "contains", "map", "filter",
    "reduce", "sort", "reverse", "join", "slice", "index_of", "find", "any",
    "all", "flatten", "enumerate", "sum", "min", "max", "unique", "concat",
    "insert", "remove"
]

let MAP_METHODS = [
    "keys", "values", "entries", "len", "has", "get", "set", "delete",
    "contains_key"
]

let NUMBER_METHODS = [
    "abs", "pow", "sqrt", "floor", "ceil", "round", "clamp", "to_str",
    "is_even", "is_odd"
]

-- builtin signatures for signature help
let BUILTIN_SIGS = #{
    "print": #{"label": "print(value)", "params": ["value"]},
    "println": #{"label": "println(value)", "params": ["value"]},
    "input": #{"label": "input(prompt)", "params": ["prompt"]},
    "len": #{"label": "len(collection)", "params": ["collection"]},
    "range": #{"label": "range(start, end, step?)", "params": ["start", "end", "step"]},
    "map": #{"label": "map(collection, fn)", "params": ["collection", "fn"]},
    "filter": #{"label": "filter(collection, fn)", "params": ["collection", "fn"]},
    "reduce": #{"label": "reduce(collection, fn, initial)", "params": ["collection", "fn", "initial"]},
    "sort": #{"label": "sort(collection, key?)", "params": ["collection", "key"]},
    "zip": #{"label": "zip(a, b)", "params": ["a", "b"]},
    "min": #{"label": "min(a, b)", "params": ["a", "b"]},
    "max": #{"label": "max(a, b)", "params": ["a", "b"]},
    "slice": #{"label": "slice(collection, start, end)", "params": ["collection", "start", "end"]},
    "join": #{"label": "join(collection, sep)", "params": ["collection", "sep"]},
    "split": #{"label": "split(str, sep)", "params": ["str", "sep"]},
    "replace": #{"label": "replace(str, old, new)", "params": ["str", "old", "new"]},
    "assert": #{"label": "assert(condition, message?)", "params": ["condition", "message"]},
    "panic": #{"label": "panic(message)", "params": ["message"]}
}

-- completion

fn make_completion(label, kind, detail) {
    let item = #{"label": label, "kind": kind}
    if detail != "" { item["detail"] = detail }
    return item
}

fn dot_completions(text, line, col) {
    let line_text = get_line(text, line)
    if col <= 0 { return null }

    -- find what's before the dot
    let before = line_text.slice(0, col - 1).trim()
    if before == "" { return null }

    let last_ch = before.slice(before.len() - 1, before.len())

    var methods = []
    if last_ch == "\"" or last_ch == "'" {
        methods = STRING_METHODS
    } elif last_ch == "]" {
        methods = ARRAY_METHODS
    } elif last_ch == "\}" {
        methods = MAP_METHODS
    } elif last_ch >= "0" and last_ch <= "9" {
        methods = NUMBER_METHODS
    } else {
        -- could be a variable, offer all
        methods = STRING_METHODS.concat(ARRAY_METHODS).concat(MAP_METHODS).concat(NUMBER_METHODS)
    }

    return methods.map(fn(m) { make_completion(m, 2, "") })
}

fn general_completions(text, word) {
    var items = []

    for kw in KEYWORDS {
        if word == "" or kw.starts_with(word) {
            items.push(make_completion(kw, 14, "keyword"))
        }
    }
    for b in BUILTINS {
        if word == "" or b.starts_with(word) {
            items.push(make_completion(b, 3, "builtin"))
        }
    }
    for m in MODULES {
        if word == "" or m.starts_with(word) {
            items.push(make_completion(m, 9, "module"))
        }
    }
    for t in TYPES {
        if word == "" or t.starts_with(word) {
            items.push(make_completion(t, 22, "type"))
        }
    }

    let syms = extract_symbols(text)
    for s in syms {
        if word == "" or s["name"].starts_with(word) {
            items.push(make_completion(s["name"], s["kind"], s["detail"]))
        }
    }

    return items
}

-- hover

fn handle_hover(uri, line, col) {
    let text = doc_text(uri)
    let word = get_word_at(text, line, col)
    if word == "" { return null }

    -- check keywords
    if KEYWORDS.contains(word) {
        return #{"contents": #{"kind": "markdown", "value": "```xs\n(keyword) {word}\n```"}}
    }

    -- check builtins
    if BUILTINS.contains(word) {
        var sig = word
        if BUILTIN_SIGS.has(word) { sig = BUILTIN_SIGS[word]["label"] }
        return #{"contents": #{"kind": "markdown", "value": "```xs\n(builtin) {sig}\n```"}}
    }

    -- check types
    if TYPES.contains(word) {
        return #{"contents": #{"kind": "markdown", "value": "```xs\n(type) {word}\n```"}}
    }

    -- check modules
    if MODULES.contains(word) {
        return #{"contents": #{"kind": "markdown", "value": "```xs\nimport {word}\n```"}}
    }

    -- check document symbols
    let syms = extract_symbols(text)
    for s in syms {
        if s["name"] == word {
            let detail = s["detail"]
            let hover = if detail.contains(word) { detail } else { "{detail} {word}" }
            return #{"contents": #{"kind": "markdown", "value": "```xs\n{hover}\n```"}}
        }
    }

    -- check other open documents
    for doc_uri in documents.keys() {
        if doc_uri == uri { continue }
        let other_syms = extract_symbols(doc_text(doc_uri))
        for s in other_syms {
            if s["name"] == word {
                let detail = s["detail"]
                let hover = if detail.contains(word) { detail } else { "{detail} {word}" }
                let file = uri_to_path(doc_uri)
                return #{"contents": #{"kind": "markdown", "value": "```xs\n{hover}\n```\n*from {file}*"}}
            }
        }
    }

    return null
}

-- signature help

fn handle_signature_help(uri, line, col) {
    let text = doc_text(uri)
    let line_text = get_line(text, line)
    var c = col
    if c > line_text.len() { c = line_text.len() }

    let before = line_text.slice(0, c)

    -- walk backwards to find unclosed paren and count commas
    var depth = 0
    var commas = 0
    var paren_pos = -1
    var i = before.len() - 1
    while i >= 0 {
        let ch = before.slice(i, i + 1)
        if ch == ")" { depth = depth + 1 }
        elif ch == "(" {
            if depth == 0 {
                paren_pos = i
                break
            }
            depth = depth - 1
        } elif ch == "," and depth == 0 {
            commas = commas + 1
        }
        i = i - 1
    }

    if paren_pos < 0 { return null }

    -- get function name before the paren
    let fn_before = before.slice(0, paren_pos).trim()
    var fn_name = ""
    var j = fn_before.len() - 1
    while j >= 0 and is_ident_char(fn_before.slice(j, j + 1)) {
        j = j - 1
    }
    fn_name = fn_before.slice(j + 1, fn_before.len())
    if fn_name == "" { return null }

    -- check builtins first
    if BUILTIN_SIGS.has(fn_name) {
        let sig = BUILTIN_SIGS[fn_name]
        var param_infos = []
        for p in sig["params"] {
            param_infos.push(#{"label": p})
        }
        return #{
            "signatures": [#{
                "label": sig["label"],
                "parameters": param_infos,
                "activeParameter": commas
            }],
            "activeSignature": 0,
            "activeParameter": commas
        }
    }

    -- check document symbols
    let syms = extract_symbols(text)
    for s in syms {
        if s["name"] == fn_name and s["kind"] == SYM_FUNCTION {
            let detail = s["detail"]
            -- parse params from detail like fn(a, b, c)
            let open = detail.index_of("(")
            let close = detail.index_of(")")
            if open >= 0 and close > open {
                let param_str = detail.slice(open + 1, close)
                let param_names = param_str.split(",")
                var param_infos = []
                for p in param_names {
                    let trimmed = p.trim()
                    if trimmed != "" { param_infos.push(#{"label": trimmed}) }
                }
                return #{
                    "signatures": [#{
                        "label": "{fn_name}{detail.slice(open, detail.len())}",
                        "parameters": param_infos,
                        "activeParameter": commas
                    }],
                    "activeSignature": 0,
                    "activeParameter": commas
                }
            }
        }
    }

    return null
}

-- go to definition

fn handle_definition(uri, line, col) {
    let text = doc_text(uri)
    let word = get_word_at(text, line, col)
    if word == "" { return null }

    -- search current doc first
    let syms = extract_symbols(text)
    for s in syms {
        if s["name"] == word {
            return #{
                "uri": uri,
                "range": #{
                    "start": #{"line": s["line"], "character": s["col"]},
                    "end": #{"line": s["line"], "character": s["col"] + word.len()}
                }
            }
        }
    }

    -- search other open docs
    for doc_uri in documents.keys() {
        if doc_uri == uri { continue }
        let other_syms = extract_symbols(doc_text(doc_uri))
        for s in other_syms {
            if s["name"] == word {
                return #{
                    "uri": doc_uri,
                    "range": #{
                        "start": #{"line": s["line"], "character": s["col"]},
                        "end": #{"line": s["line"], "character": s["col"] + word.len()}
                    }
                }
            }
        }
    }

    return null
}

-- find references

fn find_references_in_doc(doc_uri, word) {
    let text = doc_text(doc_uri)
    let lines = text.split("\n")
    var refs = []
    var i = 0
    while i < lines.len() {
        let line = lines[i]
        var pos = 0
        loop {
            let found = find_whole_word(line, word, pos)
            if found < 0 { break }
            refs.push(#{
                "uri": doc_uri,
                "range": #{
                    "start": #{"line": i, "character": found},
                    "end": #{"line": i, "character": found + word.len()}
                }
            })
            pos = found + word.len()
        }
        i = i + 1
    }
    return refs
}

fn handle_references(uri, line, col) {
    let text = doc_text(uri)
    let word = get_word_at(text, line, col)
    if word == "" { return [] }

    var all_refs = []
    for doc_uri in documents.keys() {
        let refs = find_references_in_doc(doc_uri, word)
        all_refs = all_refs.concat(refs)
    }
    return all_refs
}

-- document symbols (for outline)

fn handle_document_symbols(uri) {
    let text = doc_text(uri)
    let syms = extract_symbols(text)
    return syms.map(fn(s) {
        return #{
            "name": s["name"],
            "kind": s["kind"],
            "location": #{
                "uri": uri,
                "range": #{
                    "start": #{"line": s["line"], "character": s["col"]},
                    "end": #{"line": s["line"], "character": s["col"] + s["name"].len()}
                }
            }
        }
    })
}

-- formatting

fn handle_formatting(uri) {
    let text = doc_text(uri)
    if text == "" { return [] }

    io.write_file(TMP_FMT, text)
    let res = process.run("{XS_BIN} fmt {TMP_FMT} 2>&1")

    if not res["ok"] {
        try { io.delete_file(TMP_FMT) } catch e {}
        return []
    }

    let formatted = io.read_file(TMP_FMT)
    try { io.delete_file(TMP_FMT) } catch e {}

    let lines = text.split("\n")
    return [#{
        "range": #{
            "start": #{"line": 0, "character": 0},
            "end": #{"line": lines.len(), "character": 0}
        },
        "newText": formatted
    }]
}

-- rename

fn handle_rename(uri, line, col, new_name) {
    let text = doc_text(uri)
    let word = get_word_at(text, line, col)
    if word == "" { return null }

    var changes = #{}
    for doc_uri in documents.keys() {
        let refs = find_references_in_doc(doc_uri, word)
        if refs.len() > 0 {
            changes[doc_uri] = refs.map(fn(r) {
                return #{"range": r["range"], "newText": new_name}
            })
        }
    }

    return #{"changes": changes}
}

-- code actions (placeholder)

fn handle_code_actions(uri, range, context) {
    return []
}

-- completion dispatcher

fn handle_completion(uri, line, col, trigger) {
    let text = doc_text(uri)

    if trigger == "." {
        let items = dot_completions(text, line, col)
        if items != null { return items }
    }

    if trigger == ":" {
        -- could be enum variant, offer symbols that are enums
        let syms = extract_symbols(text)
        var items = []
        for s in syms {
            if s["kind"] == SYM_ENUM {
                items.push(make_completion(s["name"], s["kind"], s["detail"]))
            }
        }
        if items.len() > 0 { return items }
    }

    let word = get_word_at(text, line, col)
    return general_completions(text, word)
}

-- initialize

fn handle_initialize(params) {
    return #{
        "capabilities": #{
            "textDocumentSync": 1,
            "hoverProvider": true,
            "completionProvider": #{
                "triggerCharacters": [".", ":", ","],
                "resolveProvider": false
            },
            "signatureHelpProvider": #{
                "triggerCharacters": ["(", ","]
            },
            "definitionProvider": true,
            "referencesProvider": true,
            "documentSymbolProvider": true,
            "documentFormattingProvider": true,
            "renameProvider": true,
            "codeActionProvider": true
        },
        "serverInfo": #{
            "name": "xs-language-server",
            "version": "0.1.0"
        }
    }
}

-- message dispatch

fn handle_message(msg) {
    let method = msg.get("method", "")
    let id = msg.get("id", null)
    let params = msg.get("params", #{})

    match method {
        "initialize" => {
            send_response(id, handle_initialize(params))
        }
        "initialized" => {
            log("client initialized")
        }
        "shutdown" => {
            send_response(id, null)
        }
        "exit" => {
            return false
        }
        "textDocument/didOpen" => {
            let td = params["textDocument"]
            doc_open(td["uri"], td["text"], td.get("version", 0))
            log("opened {td["uri"]}")
            try { run_diagnostics(td["uri"]) } catch e { log("diagnostics error: {e}") }
        }
        "textDocument/didChange" => {
            let td = params["textDocument"]
            let changes = params["contentChanges"]
            if changes.len() > 0 {
                doc_update(td["uri"], changes[0]["text"], td.get("version", 0))
                try { run_diagnostics(td["uri"]) } catch e { log("diagnostics error: {e}") }
            }
        }
        "textDocument/didClose" => {
            let td = params["textDocument"]
            -- clear diagnostics on close
            send_notification("textDocument/publishDiagnostics", #{
                "uri": td["uri"], "diagnostics": []
            })
            doc_close(td["uri"])
            log("closed {td["uri"]}")
        }
        "textDocument/didSave" => {
            let td = params["textDocument"]
            try { run_diagnostics(td["uri"]) } catch e { log("diagnostics error: {e}") }
        }
        "textDocument/hover" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let result = handle_hover(td["uri"], pos["line"], pos["character"])
            send_response(id, result)
        }
        "textDocument/completion" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let ctx = params.get("context", #{})
            let trigger = ctx.get("triggerCharacter", "")
            let items = handle_completion(td["uri"], pos["line"], pos["character"], trigger)
            send_response(id, #{"isIncomplete": false, "items": items})
        }
        "textDocument/signatureHelp" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let result = handle_signature_help(td["uri"], pos["line"], pos["character"])
            send_response(id, result)
        }
        "textDocument/definition" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let result = handle_definition(td["uri"], pos["line"], pos["character"])
            send_response(id, result)
        }
        "textDocument/references" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let result = handle_references(td["uri"], pos["line"], pos["character"])
            send_response(id, result)
        }
        "textDocument/documentSymbol" => {
            let td = params["textDocument"]
            let result = handle_document_symbols(td["uri"])
            send_response(id, result)
        }
        "textDocument/formatting" => {
            let td = params["textDocument"]
            let result = handle_formatting(td["uri"])
            send_response(id, result)
        }
        "textDocument/rename" => {
            let td = params["textDocument"]
            let pos = params["position"]
            let new_name = params["newName"]
            let result = handle_rename(td["uri"], pos["line"], pos["character"], new_name)
            send_response(id, result)
        }
        "textDocument/codeAction" => {
            let td = params["textDocument"]
            let result = handle_code_actions(td["uri"], params["range"], params.get("context", #{}))
            send_response(id, result)
        }
        _ => {
            if id != null {
                send_error(id, -32601, "method not found: {method}")
            }
        }
    }

    return true
}

-- main loop

fn main() {
    log("xs language server starting (pid {os.pid()})")

    var running = true
    while running {
        let msg = read_message()
        if msg == null {
            log("stdin closed, shutting down")
            break
        }

        try {
            let cont = handle_message(msg)
            if cont == false { running = false }
        } catch e {
            log("error handling message: {e}")
            let id = msg.get("id", null)
            if id != null {
                send_error(id, -32603, "internal error: {e}")
            }
        }
    }

    -- cleanup temp files
    try { io.delete_file(TMP_CHECK) } catch e {}
    try { io.delete_file(TMP_FMT) } catch e {}

    log("xs language server stopped")
}

main()
