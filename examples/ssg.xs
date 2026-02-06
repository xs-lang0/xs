-- ssg.xs: minimal static site generator
-- reads content/*.md, converts to HTML, writes to build/

import fs
import path

let CONTENT_DIR = "content"
let BUILD_DIR = "build"

-- convert paired delimiters via split: "hello **bold** world" -> "hello <strong>bold</strong> world"
fn convert_pairs(text, delim, open_tag, close_tag) {
    let parts = text.split(delim)
    if parts.len() <= 1 { return text }
    var out = parts[0]
    for i in 1..parts.len() {
        if i % 2 == 1 { out = out ++ open_tag ++ parts[i] }
        else { out = out ++ close_tag ++ parts[i] }
    }
    if parts.len() % 2 == 0 { out = out ++ close_tag }
    return out
}

fn convert_inline(line) {
    var out = line
    out = convert_pairs(out, "**", "<strong>", "</strong>")
    out = convert_pairs(out, "`", "<code>", "</code>")
    out = convert_pairs(out, "*", "<em>", "</em>")
    return out
}

fn markdown_to_html(source) {
    let lines = source.split("\n")
    var html_parts = []
    var in_paragraph = false

    for line in lines {
        let trimmed = line.trim()

        if trimmed == "" {
            if in_paragraph {
                html_parts.push("</p>")
                in_paragraph = false
            }
        } elif trimmed == "---" {
            if in_paragraph {
                html_parts.push("</p>")
                in_paragraph = false
            }
            html_parts.push("<hr>")
        } elif trimmed.starts_with("## ") {
            if in_paragraph {
                html_parts.push("</p>")
                in_paragraph = false
            }
            let text = convert_inline(trimmed.slice(3, trimmed.len()))
            html_parts.push("<h2>{text}</h2>")
        } elif trimmed.starts_with("# ") {
            if in_paragraph {
                html_parts.push("</p>")
                in_paragraph = false
            }
            let text = convert_inline(trimmed.slice(2, trimmed.len()))
            html_parts.push("<h1>{text}</h1>")
        } else {
            let content = convert_inline(trimmed)
            if !in_paragraph {
                html_parts.push("<p>{content}")
                in_paragraph = true
            } else {
                html_parts.push("\n{content}")
            }
        }
    }

    if in_paragraph { html_parts.push("</p>") }
    return html_parts.join("\n")
}

fn wrap_html(title, body) {
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<style>
  body \{ max-width: 680px; margin: 2rem auto; padding: 0 1rem;
         font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
         line-height: 1.6; color: #333; \}
  h1 \{ border-bottom: 2px solid #eee; padding-bottom: 0.3rem; \}
  h2 \{ color: #555; \}
  code \{ background: #f4f4f4; padding: 0.15em 0.3em; border-radius: 3px;
          font-size: 0.9em; \}
  hr \{ border: none; border-top: 1px solid #ddd; margin: 2rem 0; \}
  strong \{ color: #111; \}
  a \{ color: #0366d6; \}
</style>
</head>
<body>
{body}
</body>
</html>"""
}

fn build_site() {
    if !fs.exists(CONTENT_DIR) {
        println("error: {CONTENT_DIR}/ directory not found")
        return
    }

    fs.mkdir(BUILD_DIR)

    let files = fs.ls(CONTENT_DIR)
    let md_files = files.filter(fn(f) { f.ends_with(".md") })

    if md_files.len() == 0 {
        println("no .md files found in {CONTENT_DIR}/")
        return
    }

    println("building {md_files.len()} page(s)...")

    for filename in md_files {
        let src_path = path.join(CONTENT_DIR, filename)
        let stem = path.stem(filename)
        let out_path = path.join(BUILD_DIR, stem ++ ".html")

        let source = fs.read(src_path)
        let body = markdown_to_html(source)
        let title = stem.replace("-", " ").replace("_", " ").capitalize()
        let html = wrap_html(title, body)

        fs.write(out_path, html)
        println("  {src_path} -> {out_path}")
    }

    println("done.")
}

build_site()
