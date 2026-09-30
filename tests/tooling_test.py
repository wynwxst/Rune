#!/usr/bin/env python3
"""End-to-end test of the editor tooling: `rune-lint` and `rune-lsp`.

The linter is run over files with known problems and its report, JSON and
`--fix` are checked. The language server is driven the way an editor drives
it — over stdin and stdout, with Content-Length framing — through a whole
session: open, diagnostics, completion, hover, definition, references,
signature help, symbols, code actions, save, close, shutdown.

    python3 tests/tooling_test.py build-release/bin

The directory must hold `rune-lint`, `rune-lsp`, `runec` and `rune`. CTest
runs this as `rune_tooling`.
"""
import json, os, re, subprocess, sys, tempfile, threading, queue

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build-release/bin")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STDLIB = os.path.join(ROOT, "stdlib")
LINT = os.path.join(BIN, "rune-lint")
LSP = os.path.join(BIN, "rune-lsp")
FMT = os.path.join(BIN, "rune-fmt")

failures = []

def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n    " + str(detail)[:2000] if detail != "" else ""))

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="") as f:
        f.write(text)

#===------------------------------------------------------------------===#
# rune-lint
#===------------------------------------------------------------------===#

LINTME = """import std::io
import std::math

fn helper_fn() -> i64 { 1 }

fn main() -> i64 {
    let unused = 3
    var never = 4
    @lint(allow(unused-variable))
    let quiet = 5
    if never > 1 == true { io::println("yes") }
    while true {
        break
    }
    return 0
}
"""

def lint_tests(tmp):
    d = os.path.join(tmp, "lint")
    src = os.path.join(d, "lintme.rune")
    write(src, LINTME)

    r = subprocess.run([LINT, "--list"], capture_output=True, text=True)
    check("lint --list names the rules", r.returncode == 0 and "unused-variable" in r.stdout and "never-reassigned" in r.stdout, r.stdout)

    r = subprocess.run([LINT, "--stdlib", STDLIB, src], capture_output=True, text=True)
    out = r.stdout
    check("lint exits 1 when it finds something", r.returncode == 1, r.returncode)
    for rule in ["unused-import", "dead-code", "unused-variable", "never-reassigned",
                 "bool-comparison", "while-true", "needless-return", "naming"]:
        check(f"lint reports {rule}", f"[{rule}]" in out, out)
    check("an @lint directive silences its statement", "`quiet`" not in out, out)
    check("human output points at the line", "7 ║     let unused = 3" in out, out)

    r = subprocess.run([LINT, "--stdlib", STDLIB, "--format", "json", src], capture_output=True, text=True)
    objs = [json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]
    check("json output is one object per finding", len(objs) >= 8, r.stdout)
    unused = [o for o in objs if o.get("code") == "unused-variable"]
    check("json carries positions", unused and unused[0]["line"] == 7 and unused[0]["column"] == 8, unused)
    check("json carries fixes", unused and "fix" in unused[0] and unused[0]["fix"]["edits"], unused)

    r = subprocess.run([LINT, "--stdlib", STDLIB, "-A", "all", "-W", "while-true", src], capture_output=True, text=True)
    check("-A all -W one leaves only that rule", "[while-true]" in r.stdout and "[unused-variable]" not in r.stdout, r.stdout)

    r = subprocess.run([LINT, "--stdlib", STDLIB, "-A", "no-such-rule", src], capture_output=True, text=True)
    check("an unknown rule is refused", r.returncode == 2, r.stderr)

    r = subprocess.run([LINT, "--stdlib", STDLIB, "--fix", src], capture_output=True, text=True)
    fixed = open(src).read()
    check("--fix rewrites the file", "let never = 4" in fixed and "loop {" in fixed and "let _unused" in fixed
          and "import std::math" not in fixed and "    0\n}" in fixed, fixed)
    rc = subprocess.run([os.path.join(BIN, "runec"), "--stdlib", STDLIB, "--check", src], capture_output=True, text=True)
    check("the fixed file still compiles", rc.returncode == 0, rc.stderr)

    # A package's [lint] table applies to its files.
    pkg = os.path.join(tmp, "lintpkg")
    write(os.path.join(pkg, "Rune.toml"), '[package]\nname = "lintpkg"\nversion = "0.1.0"\n\n[lint]\nallow = ["unused-variable"]\nwarn = ["missing-docs"]\n')
    write(os.path.join(pkg, "src", "main.rune"), "pub fn documented() -> i64 { 1 }\n\nfn main() -> i64 {\n    let x = documented()\n    0\n}\n")
    r = subprocess.run([LINT, "--stdlib", STDLIB], cwd=pkg, capture_output=True, text=True)
    check("[lint] allow turns a rule off", "[unused-variable]" not in r.stdout, r.stdout)
    check("[lint] warn turns a rule on", "[missing-docs]" in r.stdout, r.stdout)

    # `@lint` directives: file-wide only at the very top with nothing but
    # directives and imports after them; otherwise they cover the next item.
    def lint_text(name, text, *flags):
        f = os.path.join(d, name)
        write(f, text)
        return subprocess.run([LINT, "--stdlib", STDLIB, *flags, f], capture_output=True, text=True).stdout

    out = lint_text("filewide.rune", "// header\n@lint(allow(unused-variable))\n\nimport std::io\n\n"
                    "fn a() -> i64 {\n    let x = 1\n    0\n}\n\nfn main() -> i64 {\n    let y = 2\n    io::println(a())\n    0\n}\n")
    check("@lint at the top, before the imports, covers the whole file", "[unused-variable]" not in out, out)

    out = lint_text("item.rune", "@lint(allow(unused-variable))\nfn a() -> i64 {\n    let x = 1\n    0\n}\n\n"
                    "fn main() -> i64 {\n    let y = 2\n    a()\n}\n")
    check("@lint at the top, straight before a declaration, covers only it",
          "`x`" not in out and "`y`" in out, out)

    out = lint_text("stmt.rune", "fn main() -> i64 {\n    @lint(allow(unused-variable))\n    let x = 1\n    let y = 2\n    0\n}\n")
    check("@lint before a statement covers only that statement", "`x`" not in out and "`y`" in out, out)

    out = lint_text("nested.rune", "@lint(allow(all))\nfn main() -> i64 {\n    @lint(warn(unused-variable))\n"
                    "    let x = 1\n    var y = 2\n    y\n}\n")
    check("an inner @lint overrides an outer one", "`x`" in out and "[never-reassigned]" not in out, out)

    out = lint_text("invalid.rune", "@lint(allow(unused-varaible))\nfn main() -> i64 {\n    0\n}\n")
    check("an unknown rule in @lint is reported", "[invalid-lint]" in out and "unused-varaible" in out, out)

    out = lint_text("dangling.rune", "fn main() -> i64 {\n    0\n    @lint(allow(todo))\n}\n")
    check("an @lint with nothing after it is reported", "[invalid-lint]" in out, out)

    for name in ["filewide.rune", "item.rune", "stmt.rune", "nested.rune"]:
        rc = subprocess.run([os.path.join(BIN, "runec"), "--stdlib", STDLIB, "--check", os.path.join(d, name)],
                            capture_output=True, text=True)
        check(f"the compiler accepts @lint in {name}", rc.returncode == 0, rc.stderr)

    clean = os.path.join(d, "clean.rune")
    write(clean, "import std::io\n\nfn main() -> i64 {\n    io::println(\"hi\")\n    0\n}\n")
    r = subprocess.run([LINT, "--stdlib", STDLIB, clean], capture_output=True, text=True)
    check("a clean file exits 0", r.returncode == 0 and "No problems" in r.stderr, r.stdout + r.stderr)

#===------------------------------------------------------------------===#
# rune-fmt
#===------------------------------------------------------------------===#

FMTME = """fn area(width: i64, height: i64) -> i64 { width * height }
import std::io
import std::collections::vector as vec

fn first<T>(items: &vec::Vector<T>) -> T? {
    let n = items.length()
    items.get(0)
}

fn main() -> i64 {
  let a = area(3, 4);
    var list = vec::Vector<i64>()
        list.push((a + 1) * 2)
    let total = area(1,
                2)
    while true {
        break
    }
    let f = ||(n: i64) -> bool { n > 0 }
    io::println(format!("{} {}", total, f(a)))
    0
}
"""

FMTED = """import std::collections::vector as vec
import std::io

fn area(width: i64, height: i64) -> i64 { width * height }

fn first<T>(items: &vec::Vector<T>) -> T? {
    let n = items.length()
    items.get(0)
}

fn main() -> i64 {
    let a: i64 = area(width: 3, height: 4)
    var list: vec::Vector<i64> = vec::Vector<i64>()
    list.push(value: (a + 1) * 2)
    let total: i64 = area(width: 1,
        height: 2)
    loop {
        break
    }
    let f: @function(i64) -> bool = ||(n: i64) -> bool { n > 0 }
    io::println(value: format!("{} {}", total, f(a)))
    0
}
"""

def fmt_tests(tmp):
    d = os.path.join(tmp, "fmt")
    src = os.path.join(d, "fmtme.rune")
    write(src, FMTME)
    runec = os.path.join(BIN, "runec")
    base = [FMT, "--stdlib", STDLIB, "--runec", runec]

    r = subprocess.run(base + ["--check", src], capture_output=True, text=True)
    check("fmt --check lists an unformatted file and exits 1", r.returncode == 1 and "fmtme.rune" in r.stdout, r)
    check("fmt --check changes nothing", open(src).read() == FMTME)

    r = subprocess.run(base + [src], capture_output=True, text=True)
    out = open(src).read()
    check("fmt formats the file", r.returncode == 0 and out == FMTED, out)
    check("imports first and sorted, indentation, a stray `;` and `while true`",
          out.startswith("import std::collections::vector as vec\nimport std::io\n") and "loop {" in out, out)
    check("a type is written through the file's own import alias", "var list: vec::Vector<i64>" in out, out)
    check("labels go on positional arguments, a bracketed one included", "list.push(value: (a + 1) * 2)" in out, out)
    check("a generic function's bindings are left alone", "    let n = items.length()" in out, out)
    check("a macro's arguments are left alone", 'format!("{} {}", total, f(a))' in out, out)
    rc = subprocess.run([runec, "--stdlib", STDLIB, "--check", src], capture_output=True, text=True)
    check("the formatted file compiles", rc.returncode == 0, rc.stderr)

    r = subprocess.run(base + ["--check", src], capture_output=True, text=True)
    check("formatting a formatted file changes nothing", r.returncode == 0 and r.stdout == "", r)

    r = subprocess.run(base + ["--stdout", "--no-types", "--no-labels", "--no-reorder", src + ""], capture_output=True, text=True)
    check("--stdout prints, and writes nothing", r.stdout == FMTED and open(src).read() == FMTED, r.stdout)

    write(src, FMTME)
    r = subprocess.run(base + ["--stdout", "--no-types", "--no-labels", "--no-reorder", "--no-lint-fixes", src],
                       capture_output=True, text=True)
    check("--no-* leave out what they name", "let a = area(3, 4);" in r.stdout and "while true" in r.stdout
          and r.stdout.startswith("fn area") and "    var list = vec" in r.stdout, r.stdout)

    # A file that does not compile is laid out, and told so.
    broken = os.path.join(d, "broken.rune")
    write(broken, "fn main() -> i64 {\n  let x = nowhere(1)\n    0\n}\n")
    r = subprocess.run(base + [broken], capture_output=True, text=True)
    check("a file that does not compile is laid out only, and says so",
          open(broken).read() == "fn main() -> i64 {\n    let x = nowhere(1)\n    0\n}\n" and "laid out only" in r.stderr, r.stderr)

    # A header comment and the directives stay first; a signature that wraps
    # opens its body one level in.
    header = os.path.join(d, "header.rune")
    write(header, "@type(Executable)\n// What this is.\n\nimport std::io\n\n"
                  "fn add(a: i64,\n        b: i64) -> i64 {\n            a + b\n}\n\n"
                  "fn main() -> i64 {\n    io::println(value: add(a: 1, b: 2))\n    0\n}\n")
    r = subprocess.run(base + ["--stdout", header], capture_output=True, text=True)
    check("directives, then the preamble, then imports", r.stdout.startswith("@type(Executable)\n\n// What this is.\n\nimport std::io\n"), r.stdout)
    check("a wrapped signature's body is one level in", "    b: i64) -> i64 {\n    a + b\n}" in r.stdout, r.stdout)

    # `rune fmt` runs it with the toolchain's own paths.
    write(src, FMTME)
    r = subprocess.run([os.path.join(BIN, "rune"), "fmt", src], capture_output=True, text=True)
    check("`rune fmt` formats", open(src).read() == FMTED, r.stderr)

#===------------------------------------------------------------------===#
# rune-lsp
#===------------------------------------------------------------------===#

class Client:
    def __init__(self):
        self.p = subprocess.Popen([LSP, "--stdlib", STDLIB, "--runec", os.path.join(BIN, "runec"),
                                   "--rune", os.path.join(BIN, "rune")],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.next_id = 0
        self.inbox = queue.Queue()
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        out = self.p.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    self.inbox.put(None)
                    return
                line = line.strip()
                if not line:
                    break
                k, _, v = line.decode().partition(":")
                if k.lower() == "content-length":
                    length = int(v)
            body = out.read(length)
            self.inbox.put(json.loads(body))

    def send(self, msg):
        data = json.dumps(msg).encode()
        self.p.stdin.write(b"Content-Length: %d\r\n\r\n" % len(data) + data)
        self.p.stdin.flush()

    def request(self, method, params):
        self.next_id += 1
        rid = self.next_id
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        while True:
            msg = self.wait()
            if msg is None:
                raise RuntimeError("server went away during " + method)
            if msg.get("id") == rid:
                return msg
            self.notes.append(msg)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def wait(self, timeout=30):
        try:
            return self.inbox.get(timeout=timeout)
        except queue.Empty:
            return None

    def diagnostics(self, uri, source=None, timeout=30):
        """The next publishDiagnostics for `uri`, from `source` if given."""
        for i, msg in enumerate(self.notes):
            if msg.get("method") == "textDocument/publishDiagnostics" and msg["params"]["uri"] == uri:
                ds = msg["params"]["diagnostics"]
                if source is None or any(d.get("source") == source for d in ds):
                    del self.notes[: i + 1]
                    return ds
        while True:
            msg = self.wait(timeout)
            if msg is None:
                return None
            if msg.get("method") == "textDocument/publishDiagnostics" and msg["params"]["uri"] == uri:
                ds = msg["params"]["diagnostics"]
                if source is None or any(d.get("source") == source for d in ds):
                    return ds

    notes = []

    def drain(self):
        """Forgets every message already sent, so the next wait sees only
        what the next action causes."""
        self.notes = []
        while True:
            msg = self.wait(timeout=0.5)
            if msg is None:
                return

def uri_of(path):
    return "file://" + path.replace(" ", "%20")

MAIN = """import std::io
import std::collections::vector

/// A point on the plane.
struct Point {
    pub x: i64
    pub y: i64
}

extend Point {
    /// The sum of the coordinates.
    pub fn sum(&self) -> i64 { self.x + self.y }
}

fn add(a: i64, b: i64) -> i64 {
    a + b
}

fn main() -> i64 {
    let p = Point { x: 1, y: 2 }
    var list = vector::Vector<i64>()
    list.push(add(p.x, p.sum()))
    let count = list.length()
    io::println(count)
    0
}
"""

def pos(text, needle, delta=0, nth=0):
    """The LSP position of the `nth` occurrence of `needle` in `text`, plus `delta`."""
    at = -1
    for _ in range(nth + 1):
        at = text.index(needle, at + 1)
    at += delta
    line = text.count("\n", 0, at)
    col = at - (text.rfind("\n", 0, at) + 1)
    return {"line": line, "character": col}

def lsp_tests(tmp):
    d = os.path.join(tmp, "lsp dir")      # a space, to exercise URIs
    path = os.path.join(d, "main.rune")
    write(path, MAIN)
    uri = uri_of(path)
    c = Client()
    c.notes = []

    init = c.request("initialize", {"processId": None, "rootUri": uri_of(d), "capabilities": {}})
    caps = init.get("result", {}).get("capabilities", {})
    check("initialize announces the capabilities", caps.get("completionProvider") and caps.get("hoverProvider")
          and caps.get("definitionProvider") and caps.get("codeActionProvider"), init)
    c.notify("initialized", {})

    c.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "rune", "version": 1, "text": MAIN}})
    ds = c.diagnostics(uri)
    check("a clean file publishes no diagnostics", ds == [], ds)

    doc = {"uri": uri}
    r = c.request("textDocument/completion", {"textDocument": doc, "position": pos(MAIN, "list.push", 5)})
    labels = [i["label"] for i in r["result"]["items"]]
    check("completion after `list.` offers Vector's methods", "push" in labels and "length" in labels and "pop" in labels, labels)

    r = c.request("textDocument/completion", {"textDocument": doc, "position": pos(MAIN, "p.sum", 2)})
    labels = [i["label"] for i in r["result"]["items"]]
    check("completion after `p.` offers fields and extend methods", "x" in labels and "y" in labels and "sum" in labels, labels)

    r = c.request("textDocument/completion", {"textDocument": doc, "position": pos(MAIN, "io::println", 4)})
    labels = [i["label"] for i in r["result"]["items"]]
    check("completion after `io::` offers the module's functions", "println" in labels and "readLine" in labels, labels)

    r = c.request("textDocument/completion", {"textDocument": doc, "position": pos(MAIN, "    0\n}", 4)})
    labels = [i["label"] for i in r["result"]["items"]]
    check("completion in a body offers locals, functions, keywords",
          all(x in labels for x in ["p", "list", "count", "add", "main", "Point", "io", "return", "Some"]), labels)

    # Member completion asks the compiler, so it knows what the index alone
    # cannot: superclasses, what a generic chain returns, what a bind adds,
    # a closure parameter's inferred type. Its signatures leave `self` out,
    # which is how these checks tell its answer from the index's.
    def members_after(text, anchor, typed):
        at = text.index(anchor) + len(anchor)
        edited = text[:at] + typed + "\n" + text[at:]
        c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 50},
                                            "contentChanges": [{"text": edited}]})
        end = at + len(typed)
        line = edited.count("\n", 0, end)
        col = end - (edited.rfind("\n", 0, end) + 1)
        r = c.request("textDocument/completion", {"textDocument": doc, "position": {"line": line, "character": col}})
        items = r["result"]["items"]
        return {i["label"]: i.get("detail", "") for i in items}
    inherit = MAIN.replace("fn add(", "class Base {\n    pub id: i64\n    fn init(self) { self.id = 1 }\n    pub fn ident(&self) -> i64 { self.id }\n}\n\nclass Derived : Base {\n    pub extra: bool\n    fn init(self) { super.init(); self.extra = true }\n}\n\nfn add(")
    got = members_after(inherit, "    let count = list.length()\n", "    Derived().")
    check("member completion includes a superclass's fields and methods",
          all(k in got for k in ["extra", "id", "ident"]) and got.get("ident") == "fn ident() -> i64", got)
    got = members_after(MAIN, "    let count = list.length()\n", "    list.values().")
    check("member completion follows a generic chain to the iterator's methods",
          all(k in got for k in ["map", "filter", "collect"]), sorted(got)[:20])
    got = members_after(MAIN, "    let count = list.length()\n", "    list.")
    check("a type's private fields are not offered outside its module",
          "block" not in got and "count" not in got and "push" in got, sorted(got))
    closure = MAIN.replace("    let count = list.length()\n", "    let doubled = list.values().map(||(v) { v * 2 }).collect()\n    let count = list.length()\n")
    got = members_after(closure, "map(||(v) { ", "v.")
    check("member completion knows a closure parameter's inferred type",
          "$wrappingAdd" in got and "display" in got, sorted(got))
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 51}, "contentChanges": [{"text": MAIN}]})

    r = c.request("textDocument/hover", {"textDocument": doc, "position": pos(MAIN, "sum()", 1)})
    value = (r.get("result") or {}).get("contents", {}).get("value", "")
    check("hover on a method shows its signature and doc", "fn sum(&self) -> i64" in value and "sum of the coordinates" in value, value)

    r = c.request("textDocument/hover", {"textDocument": doc, "position": pos(MAIN, "list.push", 1)})
    value = (r.get("result") or {}).get("contents", {}).get("value", "")
    check("hover on a local shows its inferred type", "list: vector::Vector<i64>" in value, value)

    r = c.request("textDocument/hover", {"textDocument": doc, "position": pos(MAIN, "import std::io", 12)})
    value = (r.get("result") or {}).get("contents", {}).get("value", "")
    check("hover on an import shows the module", "import std::io" in value, value)

    r = c.request("textDocument/definition", {"textDocument": doc, "position": pos(MAIN, "add(p.x", 1)})
    loc = r.get("result") or {}
    check("definition of a function is its declaration", loc.get("uri") == uri and loc["range"]["start"]["line"] == 14, loc)

    r = c.request("textDocument/definition", {"textDocument": doc, "position": pos(MAIN, "list.push", 6)})
    loc = r.get("result") or {}
    check("definition reaches into the standard library", loc.get("uri", "").endswith("collections/vector.rune"), loc)

    r = c.request("textDocument/references", {"textDocument": doc, "position": pos(MAIN, "let p", 4),
                                              "context": {"includeDeclaration": True}})
    refs = r.get("result") or []
    check("references to a local", len(refs) == 3, refs)

    r = c.request("textDocument/signatureHelp", {"textDocument": doc, "position": pos(MAIN, "add(p.x, ", 9)})
    sig = r.get("result") or {}
    check("signature help shows the parameters and which one",
          sig.get("signatures") and sig["signatures"][0]["label"].startswith("fn add") and sig.get("activeParameter") == 1, sig)

    r = c.request("textDocument/documentSymbol", {"textDocument": doc})
    names = [s["name"] for s in r.get("result") or []]
    point = [s for s in r.get("result") or [] if s["name"] == "Point"]
    check("document symbols list the declarations", all(x in names for x in ["Point", "add", "main"]), names)
    check("a struct's members are its children",
          point and sorted(ch["name"] for ch in point[0].get("children", [])) == ["sum", "x", "y"], point)

    # An edit that introduces lint problems: diagnostics follow the text.
    edited = MAIN.replace("    let count = list.length()\n", "    let count = list.length()\n    var spare = 1\n")
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": edited}]})
    ds = c.diagnostics(uri, "rune-lint")
    codes = [d.get("code") for d in (ds or [])]
    check("lint diagnostics follow unsaved edits", "unused-variable" in codes, ds)

    r = c.request("textDocument/codeAction", {"textDocument": doc, "range": {"start": pos(edited, "spare"), "end": pos(edited, "spare")},
                                              "context": {"diagnostics": ds or []}})
    titles = [a["title"] for a in r.get("result") or []]
    check("a quick fix is offered for the finding", any("underscore" in t for t in titles), titles)
    check("and a fix-all source action", any("Fix all" in t for t in titles), titles)

    # A compile error typed but not saved: once typing pauses, the compiler
    # checks the buffer as it stands.
    unsaved = edited.replace("add(p.x, p.sum())", "add(p.x, nowhere)")
    c.drain()
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3}, "contentChanges": [{"text": unsaved}]})
    ds = c.diagnostics(uri, "runec")
    errs = [d for d in (ds or []) if d.get("source") == "runec"]
    check("an unsaved error is reported once typing pauses", errs and "nowhere" in errs[0]["message"], ds)
    c.drain()
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 4}, "contentChanges": [{"text": edited}]})
    ds = c.diagnostics(uri)
    while ds is not None and any(d.get("source") == "runec" for d in ds):
        ds = c.diagnostics(uri, timeout=5)
    check("and cleared when it is typed away", ds is not None, ds)

    # A compile error, written and saved: the compiler's diagnostics arrive.
    broken = edited.replace("add(p.x, p.sum())", "add(p.x, missing)")
    write(path, broken)
    c.drain()
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3}, "contentChanges": [{"text": broken}]})
    c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    ds = c.diagnostics(uri, "runec")
    errs = [d for d in (ds or []) if d.get("source") == "runec"]
    check("saving runs the compiler", errs and "missing" in errs[0]["message"] and errs[0]["severity"] == 1, ds)
    if errs:
        at = pos(broken, "missing")
        check("the compiler's error is placed on the name", errs[0]["range"]["start"] == at, errs[0]["range"])

    write(path, edited)
    c.drain()
    c.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 4}, "contentChanges": [{"text": edited}]})
    c.notify("textDocument/didSave", {"textDocument": {"uri": uri}})
    ds = c.diagnostics(uri)
    while ds is not None and any(d.get("source") == "runec" for d in ds):
        ds = c.diagnostics(uri, timeout=5)      # the change's publish comes before the save's
    check("fixing the error clears it", ds is not None and not any(d.get("source") == "runec" for d in ds), ds)

    c.drain()
    c.notify("textDocument/didClose", {"textDocument": {"uri": uri}})
    ds = c.diagnostics(uri)
    check("closing clears the file's diagnostics", ds == [], ds)

    # Inside a package the compiler runs as `rune check`, which knows the
    # package's other files: an error in one module is reported there.
    pkg = os.path.join(tmp, "lsppkg")
    write(os.path.join(pkg, "Rune.toml"), '[package]\nname = "lsppkg"\nversion = "0.1.0"\nedition = "2025"\n')
    write(os.path.join(pkg, "src", "shapes.rune"), "pub fn area(w: i64, h: i64) -> i64 { w * h }\n")
    main_src = "import std::io\nimport lsppkg::shapes\n\nfn main() -> i64 {\n    io::println(shapes::area(2, \"three\"))\n    0\n}\n"
    write(os.path.join(pkg, "src", "main.rune"), main_src)
    puri = uri_of(os.path.join(pkg, "src", "main.rune"))
    c.drain()
    c.notify("textDocument/didOpen", {"textDocument": {"uri": puri, "languageId": "rune", "version": 1, "text": main_src}})
    ds = c.diagnostics(puri, "runec")
    check("a package file is checked with `rune check`", ds and any("String" in d["message"] for d in ds), ds)
    r = c.request("textDocument/completion", {"textDocument": {"uri": puri}, "position": pos(main_src, "shapes::area", 8)})
    labels = [i["label"] for i in r["result"]["items"]]
    check("completion reaches the package's own modules", "area" in labels, labels)
    r = c.request("textDocument/definition", {"textDocument": {"uri": puri}, "position": pos(main_src, "area(", 1)})
    loc = r.get("result") or {}
    check("definition reaches the package's own modules", loc.get("uri", "").endswith("src/shapes.rune"), loc)

    # In a package, the compiler is asked with the package's other modules,
    # so a type from one of them has its members.
    write(os.path.join(pkg, "src", "shapes.rune"),
          "pub struct Rect {\n    pub width: i64\n    pub height: i64\n}\n\n"
          "pub fn area(w: i64, h: i64) -> i64 { w * h }\n\n"
          "pub fn square(n: i64) -> Rect { Rect { width: n, height: n } }\n")
    rect_src = "import std::io\nimport lsppkg::shapes\n\nfn main() -> i64 {\n    let r = shapes::square(3)\n    r.\n    0\n}\n"
    c.notify("textDocument/didChange", {"textDocument": {"uri": puri, "version": 2}, "contentChanges": [{"text": rect_src}]})
    r = c.request("textDocument/completion", {"textDocument": {"uri": puri}, "position": pos(rect_src, "    r.", 6)})
    labels = {i["label"]: i.get("detail", "") for i in r["result"]["items"]}
    check("member completion in a package knows the package's own types",
          labels.get("width") == "width: i64" and "height" in labels, labels)

    # Under `[build] memory = "zombie"`, the borrow checker's errors are
    # reported like any other, with where the value went marked as a hint.
    zpkg = os.path.join(tmp, "zombiepkg")
    write(os.path.join(zpkg, "Rune.toml"), '[package]\nname = "zombiepkg"\nversion = "0.1.0"\n\n[build]\nmemory = "zombie"\n')
    zsrc = ("class Node {\n    pub v: i64\n    fn init(self, v: i64) { self.v = v }\n}\n\n"
            "fn take(n: Node) -> i64 { n.v }\n\nfn main() -> i64 {\n    let a = Node(1)\n    let b = take(a)\n    b + a.v\n}\n")
    write(os.path.join(zpkg, "src", "main.rune"), zsrc)
    zuri = uri_of(os.path.join(zpkg, "src", "main.rune"))
    c.drain()
    c.notify("textDocument/didOpen", {"textDocument": {"uri": zuri, "languageId": "rune", "version": 1, "text": zsrc}})
    ds = c.diagnostics(zuri, "runec") or []
    moved = [d for d in ds if d.get("code") == "E0273"]
    check("a use after move is reported in a zombie package", moved and moved[0]["severity"] == 1
          and moved[0]["range"]["start"] == pos(zsrc, "a.v"), ds)
    hints = [d for d in ds if d.get("severity") == 4 and d.get("source") == "runec"]
    check("where it was moved is marked as a hint", hints and hints[0]["range"]["start"] == pos(zsrc, "take(a)", 5)
          and hints[0].get("relatedInformation"), ds)
    check("and attached to the error as related information", moved and moved[0].get("relatedInformation"), moved)

    # Inlay hints: the types and labels `rune fmt` would write, each with
    # the edit that writes it.
    hsrc = FMTME
    hpath = os.path.join(tmp, "hints", "hints.rune")
    write(hpath, hsrc)
    huri = uri_of(hpath)
    c.notify("textDocument/didOpen", {"textDocument": {"uri": huri, "languageId": "rune", "version": 1, "text": hsrc}})
    r = c.request("textDocument/inlayHint", {"textDocument": {"uri": huri},
                                             "range": {"start": {"line": 0, "character": 0}, "end": {"line": 99, "character": 0}}})
    hints = r.get("result") or []
    byLabel = {}
    for h in hints:
        byLabel.setdefault(h["label"], h)
    check("initialize announces inlay hints and formatting", caps.get("inlayHintProvider") and caps.get("documentFormattingProvider"), caps)
    t = byLabel.get(": vec::Vector<i64>")
    check("a binding's inferred type is a type hint after its name", t and t["kind"] == 1
          and t["position"] == pos(hsrc, "var list", 8), hints)
    check("double-clicking it writes it in", t and t.get("textEdits", [{}])[0].get("newText") == ": vec::Vector<i64>", t)
    w = byLabel.get("width:")
    check("a positional argument's parameter is a parameter hint before it", w and w["kind"] == 2
          and w["position"] == pos(hsrc, "area(3", 5) and w.get("textEdits", [{}])[0].get("newText") == "width: ", hints)
    check("nothing inside a generic function", not any(h["position"]["line"] == 5 for h in hints), hints)

    # Mid-edit, when the file does not check, the last hints move with the
    # text: none is left where a line went, or on the wrong line.
    lines = hsrc.split("\n")
    at = lines.index("    var list = vec::Vector<i64>()")
    broken = "\n".join(lines[:at] + ["    let half = "] + lines[at + 1:])
    c.notify("textDocument/didChange", {"textDocument": {"uri": huri, "version": 2}, "contentChanges": [{"text": broken}]})
    r = c.request("textDocument/inlayHint", {"textDocument": {"uri": huri},
                                             "range": {"start": {"line": 0, "character": 0}, "end": {"line": 99, "character": 0}}})
    after = r.get("result") or []
    labels = [h["label"] for h in after]
    total = [h for h in after if h["label"] == ": i64" and h["position"]["line"] == at + 2]
    check("hints on a line that changed go with it", ": vec::Vector<i64>" not in labels, after)
    check("the rest stay on their own lines while the file does not check", total, after)
    c.notify("textDocument/didChange", {"textDocument": {"uri": huri, "version": 3}, "contentChanges": [{"text": hsrc}]})

    r = c.request("textDocument/formatting", {"textDocument": {"uri": huri}, "options": {"tabSize": 4, "insertSpaces": True}})
    edits = r.get("result") or []
    check("Format Document formats as `rune fmt` does", len(edits) == 1 and edits[0]["newText"] == FMTED, edits)

    r = c.request("rune/unknownRequest", {})
    check("an unknown request is an error, not a crash", r.get("error", {}).get("code") == -32601, r)

    r = c.request("shutdown", None)
    c.notify("exit", None)
    try:
        code = c.p.wait(timeout=10)
    except subprocess.TimeoutExpired:
        c.p.kill()
        code = None
    check("shutdown then exit ends the server with status 0", code == 0, code)

#===------------------------------------------------------------------===#
# The books: `rune doc lint`, `rune doc lsp`
#===------------------------------------------------------------------===#

BOOKS = os.path.join(ROOT, "tools", "books")

def book_tests(tmp):
    # Every rule has a section in the rules chapter, and the chapter's
    # example for it produces it: the book shows what the linter does.
    rules_md = open(os.path.join(BOOKS, "rune-lint", "rules.md")).read()
    listed = subprocess.run([LINT, "--list"], capture_output=True, text=True).stdout.split("\n")
    names = [l.split()[0] for l in listed if l.strip()]
    sections = {}
    for part in re.split(r"^## ", rules_md, flags=re.M)[1:]:
        title, _, body = part.partition("\n")
        m = re.search(r"```rune\n(.*?)```", body, re.S)
        sections[title.strip()] = m.group(1) if m else None
    missing = [n for n in names if n not in sections]
    check("the rules chapter has a section for every rule", not missing, missing)
    extra = [s for s in sections if s not in names]
    check("the rules chapter names no rule that does not exist", not extra, extra)
    d = os.path.join(tmp, "book-samples")
    wrong = []
    for rule, sample in sections.items():
        if sample is None:
            continue
        path = os.path.join(d, rule.replace("-", "_") + ".rune")
        write(path, sample)
        r = subprocess.run([LINT, "--stdlib", STDLIB, "-W", "all", "--format", "json", path],
                           capture_output=True, text=True)
        codes = [json.loads(l)["code"] for l in r.stdout.splitlines() if l.startswith("{")]
        if rule not in codes:
            wrong.append(f"{rule}: got {codes}")
    check("every example in the rules chapter produces its rule", not wrong, wrong)

    # Both books build and open through `rune doc`, from any directory, into
    # an isolated cache.
    home = os.path.join(tmp, "rune-home")
    env = dict(os.environ, RUNE_HOME=home)
    for name, folder, heading in [("lint", "rune-lint", "The Rune linter"),
                                  ("lsp", "rune-lsp", "The Rune language server")]:
        r = subprocess.run([os.path.join(BIN, "rune"), "doc", name, "--no-open", "--no-color"],
                           cwd=tmp, env=env, capture_output=True, text=True)
        page = os.path.join(home, "docs", folder, "index.html")
        ok = r.returncode == 0 and os.path.exists(page) and heading in open(page).read()
        check(f"`rune doc {name}` builds its book", ok, r.stdout + r.stderr)

def main():
    with tempfile.TemporaryDirectory() as tmp:
        lint_tests(tmp)
        fmt_tests(tmp)
        lsp_tests(tmp)
        book_tests(tmp)
    if failures:
        print("\n%d failure(s):" % len(failures))
        for f in failures:
            print(" -", f)
        return 1
    print("\nall tooling checks passed")
    return 0

if __name__ == "__main__":
    sys.exit(main())
