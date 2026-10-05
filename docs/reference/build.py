#!/usr/bin/env python3
"""Builds the Rune HTML reference from docs/reference/content.py.

Every code sample in the reference is handed to the real compiler before it is
rendered, and the output shown next to a sample is the output the program
actually produced. Nothing here is transcribed by hand:

    run    a whole program: compiled, linked, executed, stdout captured
    decls  declarations only: a trivial `main` is appended so it still compiles
    diag   expected to be rejected: the compiler's own diagnostic is captured
    frag   grammar or shell text, not Rune, so not compiled

Usage:
    python3 docs/reference/build.py [--out docs/rune-reference.html] [--quick]

`--quick` skips verification, for iterating on the layout alone.
"""

from __future__ import annotations

import argparse
import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import content as content_module  # noqa: E402


# ---------------------------------------------------------------------------
# Rune syntax highlighting
# ---------------------------------------------------------------------------

KEYWORDS = {
    "fn", "var", "let", "mut", "global", "class", "struct", "enum", "mark",
    "bind", "to", "extend", "super", "self", "Self", "import", "pub", "as",
    "is", "if", "elif", "else", "while", "loop", "for", "in", "match",
    "return", "break", "continue", "extern", "where", "defer", "unsafe",
    "operator", "dyn", "type", "weak",
}
LITERAL_WORDS = {"true", "false", "nil"}
BUILTIN_TYPES = {
    "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "isize", "usize",
    "f32", "f64", "bool", "Character", "CString", "String", "Void", "Never",
    "int", "uint", "float", "double", "byte", "Byte",
}
# Types the language itself depends on, worth colouring like a builtin.
LANG_TYPES = {"Option", "Result", "Some", "None", "Ok", "Err"}

TOKEN_RE = re.compile(
    r"""
    (?P<comment>//[^\n]*|/\*(?:[^*]|\*(?!/))*\*/)
  | (?P<rawstring>r\#*"(?:[^"]|"(?=\#))*"\#*)
  | (?P<string>"(?:[^"\\\n]|\\.)*")
  | (?P<char>'(?:[^'\\\n]|\\.|\\u\{[0-9A-Fa-f]+\})')
  | (?P<number>
        0[xX][0-9A-Fa-f_]+ | 0[bB][01_]+ | 0[oO][0-7_]+
      | \d[\d_]*\.\d[\d_]*(?:[eE][+-]?\d+)?(?:f32|f64)?
      | \d[\d_]*(?:[eE][+-]?\d+)?(?:i8|i16|i32|i64|u8|u16|u32|u64|isize|usize|f32|f64)?
    )
  | (?P<decorator>@[A-Za-z_][A-Za-z0-9_]*)
  | (?P<word>[A-Za-z_][A-Za-z0-9_]*)
  | (?P<punct>::|\.\.=|\.\.\.|\.\.|->|=>|\|\||&&|\?\?|<<=|>>=|==|!=|<=|>=|\+=|-=|\*=|/=|%=|&=|\|=|\^=|<<|>>|[-+*/%<>=!&|^~?:;,.@#(){}\[\]])
  | (?P<ws>\s+)
    """,
    re.VERBOSE,
)


def highlight(code: str) -> str:
    """Turns Rune source into span-wrapped HTML."""
    out: list[str] = []
    pos = 0
    prev_word: str | None = None
    while pos < len(code):
        m = TOKEN_RE.match(code, pos)
        if not m:
            out.append(html.escape(code[pos]))
            pos += 1
            continue
        pos = m.end()
        kind = m.lastgroup
        text = m.group()
        esc = html.escape(text)

        if kind == "comment":
            out.append(f'<span class="c-com">{esc}</span>')
        elif kind in ("string", "rawstring"):
            out.append(f'<span class="c-str">{esc}</span>')
        elif kind == "char":
            out.append(f'<span class="c-chr">{esc}</span>')
        elif kind == "number":
            out.append(f'<span class="c-num">{esc}</span>')
        elif kind == "decorator":
            out.append(f'<span class="c-dec">{esc}</span>')
        elif kind == "word":
            if text in KEYWORDS:
                out.append(f'<span class="c-kw">{esc}</span>')
            elif text in LITERAL_WORDS:
                out.append(f'<span class="c-lit">{esc}</span>')
            elif text in BUILTIN_TYPES or text in LANG_TYPES:
                out.append(f'<span class="c-typ">{esc}</span>')
            elif prev_word == "fn":
                out.append(f'<span class="c-fn">{esc}</span>')
            elif code[pos : pos + 1] == "(":
                out.append(f'<span class="c-call">{esc}</span>')
            elif text[:1].isupper():
                out.append(f'<span class="c-typ">{esc}</span>')
            else:
                out.append(esc)
            prev_word = text
            continue
        elif kind == "punct":
            out.append(f'<span class="c-pun">{esc}</span>')
        else:
            out.append(esc)
        if kind != "ws":
            prev_word = None
    return "".join(out)


def numbered(code: str) -> str:
    """Renders highlighted code with a line-number gutter."""
    lines = highlight(code).split("\n")
    while lines and not lines[-1].strip():
        lines.pop()
    gutter = "\n".join(str(i + 1) for i in range(len(lines)))
    body = "\n".join(lines)
    return (
        '<div class="code"><pre class="gutter" aria-hidden="true">'
        f"{gutter}</pre><pre class=\"src\"><code>{body}</code></pre></div>"
    )


# ---------------------------------------------------------------------------
# ANSI -> HTML, so a captured diagnostic keeps the compiler's own colours
# ---------------------------------------------------------------------------

ANSI_CLASS = {
    "0": None,
    "1": "a-b",
    "2": "a-dim",
    "31": "a-red",
    "1;31": "a-red a-b",
    "33": "a-yel",
    "1;33": "a-yel a-b",
    "34": "a-blu",
    "36": "a-cya",
    "32": "a-grn",
    "35": "a-mag",
    "1;35": "a-mag a-b",
}
ANSI_RE = re.compile(r"\x1b\[([0-9;]*)m")


def ansi_to_html(text: str) -> str:
    out: list[str] = []
    open_spans = 0
    pos = 0
    for m in ANSI_RE.finditer(text):
        out.append(html.escape(text[pos : m.start()]))
        pos = m.end()
        code = m.group(1)
        if code in ("", "0"):
            out.append("</span>" * open_spans)
            open_spans = 0
            continue
        cls = ANSI_CLASS.get(code)
        if cls:
            out.append(f'<span class="{cls}">')
            open_spans += 1
    out.append(html.escape(text[pos:]))
    out.append("</span>" * open_spans)
    return "".join(out)


# ---------------------------------------------------------------------------
# Verification against the real compiler
# ---------------------------------------------------------------------------


@dataclass
class Toolchain:
    runec: Path
    stdlib: Path
    workdir: Path
    enabled: bool = True
    compiled: int = 0
    failures: list[str] = field(default_factory=list)

    def _run(self, source: str, name: str, color: bool, debug: bool = False,
             safety: str = "", memory: str = ""):
        src = self.workdir / f"{name}.rune"
        src.write_text(source)
        exe = self.workdir / name
        cmd = [str(self.runec), "--stdlib", str(self.stdlib), "-o", str(exe), str(src)]
        if safety:
            cmd += ["--safety", safety]
        if memory:
            cmd += ["--memory", memory]
        cmd.append("--color" if color else "--no-color")
        if debug:
            cmd.append("-g")
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
        return proc, exe

    def verify_run(self, source: str, name: str, safety: str = "", memory: str = "") -> str:
        """Compiles and runs a whole program; returns its stdout."""
        if not self.enabled:
            return ""
        self.compiled += 1
        proc, exe = self._run(source, name, color=False, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(f"{name}: did not compile\n{proc.stderr}")
            return ""
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        if run.returncode != 0:
            self.failures.append(
                f"{name}: exited {run.returncode}\n{run.stdout}{run.stderr}"
            )
        if "still live at exit" in run.stderr:
            self.failures.append(f"{name}: leaked objects\n{run.stderr}")
        return run.stdout.rstrip("\n")

    def verify_leak(self, source: str, name: str, safety: str = "", memory: str = "") -> tuple[str, str]:
        """A program expected to finish, but to report leaked objects."""
        if not self.enabled:
            return "", ""
        self.compiled += 1
        proc, exe = self._run(source, name, color=True, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(f"{name}: did not compile\n{proc.stderr}")
            return "", ""
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        if "still live at exit" not in run.stderr:
            self.failures.append(f"{name}: expected a leak report, got none")
        return run.stdout.rstrip("\n"), run.stderr.strip("\n")

    def verify_fails(self, source: str, name: str, safety: str = "", memory: str = "") -> str:
        """A program expected to finish, but to report failure through its exit
        status — a test file with a failing check. Returns its stdout."""
        if not self.enabled:
            return ""
        self.compiled += 1
        proc, exe = self._run(source, name, color=True, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(f"{name}: did not compile\n{proc.stderr}")
            return ""
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        if run.returncode == 0:
            self.failures.append(
                f"{name}: expected a non-zero exit, but it succeeded")
            return ""
        if "still live at exit" in run.stderr:
            self.failures.append(f"{name}: leaked objects\n{run.stderr}")
        return run.stdout.rstrip("\n")

    def verify_panic(self, source: str, name: str, safety: str = "", memory: str = "") -> tuple[str, str]:
        """A program that is expected to abort; returns (stdout, panic text)."""
        if not self.enabled:
            return "", ""
        self.compiled += 1
        # `-g` so the captured panic carries its traceback.
        proc, exe = self._run(source, name, color=True, debug=True, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(f"{name}: did not compile\n{proc.stderr}")
            return "", ""
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
        if run.returncode == 0:
            self.failures.append(f"{name}: expected a panic, but it exited 0")
            return run.stdout.rstrip("\n"), ""
        return run.stdout.rstrip("\n"), run.stderr.strip("\n")

    def verify_decls(self, source: str, name: str, safety: str = "",
                     memory: str = "") -> None:
        """Type-checks declarations by appending a trivial entry point."""
        if not self.enabled:
            return
        self.compiled += 1
        harness = source + "\n\nfn main() -> i64 { 0 }\n"
        proc, _ = self._run(harness, name, color=False, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(f"{name}: did not compile\n{proc.stderr}")

    def verify_diag(self, source: str, name: str, safety: str = "", memory: str = "") -> str:
        """Compiles something expected to be rejected; returns the diagnostic."""
        if not self.enabled:
            return ""
        self.compiled += 1
        proc, _ = self._run(source, name, color=True, safety=safety, memory=memory)
        if proc.returncode == 0:
            self.failures.append(f"{name}: expected a diagnostic, but it compiled")
            return ""
        return proc.stderr.rstrip("\n")

    def verify_warn(self, source: str, name: str, safety: str = "", memory: str = "") -> str:
        """Compiles something that is accepted but reported. Returns what the
        compiler said, and fails the build if it said nothing."""
        if not self.enabled:
            return ""
        self.compiled += 1
        proc, _ = self._run(source, name, color=True, safety=safety, memory=memory)
        if proc.returncode != 0:
            self.failures.append(
                f"{name}: expected a warning, but it did not compile\n{proc.stderr}")
            return ""
        if not proc.stderr.strip():
            self.failures.append(f"{name}: expected a warning, but it was silent")
            return ""
        return proc.stderr.rstrip("\n")


# ---------------------------------------------------------------------------
# Inline prose formatting
# ---------------------------------------------------------------------------

def inline(text: str) -> str:
    """`code`, **bold**, *em*, and [label](href) inside prose."""
    parts = re.split(r"(`[^`]+`)", text)
    out: list[str] = []
    for part in parts:
        if part.startswith("`") and part.endswith("`") and len(part) > 1:
            out.append(f'<code class="inl">{highlight(part[1:-1])}</code>')
            continue
        esc = html.escape(part)
        esc = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", esc)
        esc = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", esc)
        esc = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", r'<a href="\2">\1</a>', esc)
        out.append(esc)
    return "".join(out)


# ---------------------------------------------------------------------------
# Item rendering
# ---------------------------------------------------------------------------

def slugify(text: str) -> str:
    s = re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-")
    return s or "section"


def result(cls: str, body: str, text: str) -> str:
    """A collapsed disclosure holding captured output.

    `<details>` rather than script, so it still opens with JavaScript off, and
    the handle counts lines instead of naming a category — the dimmer slab and
    the colour already say which kind of block this is.
    """
    n = len(text.strip("\n").split("\n"))
    unit = "line" if n == 1 else "lines"
    return (
        f'<details class="{cls}"><summary>{n} {unit}</summary>'
        f"<pre><code>{body}</code></pre></details>"
    )


def render_item(item, tools: Toolchain, ident: str) -> str:
    kind = item["kind"]

    if kind == "prose":
        return f'<p>{inline(item["text"])}</p>'

    if kind == "note":
        label = item.get("label", "Note")
        tone = item.get("tone", "note")
        return (
            f'<aside class="callout t-{tone}">'
            f'<span class="callout-label">{html.escape(label)}</span>'
            f'<span class="callout-body">{inline(item["text"])}</span></aside>'
        )

    if kind == "table":
        head = "".join(f"<th>{inline(h)}</th>" for h in item["headers"])
        rows = "".join(
            "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in row) + "</tr>"
            for row in item["rows"]
        )
        cap = (
            f'<figcaption>{inline(item["caption"])}</figcaption>'
            if item.get("caption")
            else ""
        )
        return (
            f'<figure class="tablewrap">{cap}'
            f"<table><thead><tr>{head}</tr></thead><tbody>{rows}</tbody></table>"
            "</figure>"
        )

    if kind == "grammar":
        return (
            f'<div class="grammar">{COPY}<pre><code>'
            f'{html.escape(item["text"].strip())}</code></pre></div>'
        )

    if kind == "shell":
        return (
            f'<div class="shell">{COPY}<pre><code>'
            f'{html.escape(item["text"].strip())}</code></pre></div>'
        )

    if kind == "sample":
        code = item["code"].strip("\n")
        mode = item.get("mode", "decls")
        safety = item.get("safety", "")
        memory = item.get("memory", "")
        title = item.get("title")
        head = (
            f'<div class="sample-head"><span class="sample-title">'
            f"{inline(title)}</span></div>"
            if title
            else ""
        )

        output_html = ""
        if mode == "run":
            out = tools.verify_run(code, ident, safety, memory)
            if out:
                output_html = result("output", html.escape(out), out)
        elif mode == "decls":
            tools.verify_decls(code, ident, safety, memory)
        elif mode in ("panic", "leak"):
            if mode == "leak":
                out, panic = tools.verify_leak(code, ident, safety, memory)
            else:
                out, panic = tools.verify_panic(code, ident, safety, memory)
            blocks = ""
            if out:
                blocks += result("output", html.escape(out), out)
            if panic:
                blocks += result("diagnostic", ansi_to_html(panic), panic)
            output_html = blocks
        elif mode == "diag":
            diag = tools.verify_diag(code, ident, safety, memory)
            if diag:
                output_html = result("diagnostic", ansi_to_html(diag), diag)
        elif mode == "fails":
            out = tools.verify_fails(code, ident, safety, memory)
            if out:
                output_html = result("output", ansi_to_html(out), out)
        elif mode == "warn":
            diag = tools.verify_warn(code, ident, safety, memory)
            if diag:
                output_html = result("diagnostic", ansi_to_html(diag), diag)

        body = numbered(code) if mode != "frag" else (
            f'<div class="code"><pre class="src"><code>{highlight(code)}</code></pre></div>'
        )
        return (f'<figure class="sample">{head}{COPY}{body}{output_html}'
                "</figure>")

    raise ValueError(f"unknown item kind {kind!r}")


# One control, repeated. The text it copies is read from the block at click
# time, so the gutter's line numbers never come along.
COPY = ('<button class="copy" type="button" aria-label="Copy to clipboard">'
        "Copy</button>")


# ---------------------------------------------------------------------------
# Page assembly
# ---------------------------------------------------------------------------

CSS = (Path(__file__).parent / "theme.css").read_text()

JS = (Path(__file__).parent / "nav.js").read_text()

# Runs in the head, so the remembered palette is in place before the first
# paint rather than a moment after it.
BOOT = (Path(__file__).parent / "boot.js").read_text()

# There is no header bar, so the settings sit at the top of the right rail.
SETTINGS = """<section class="settings" aria-label="Settings">
      <h4>Settings</h4>
      <div class="fields">
        <label class="setting"><span>Theme</span>
          <select id="theme">
            <option value="auto">Auto</option>
            <option value="light">Light</option>
            <option value="dark">Dark</option>
            <option value="ocean">Ocean</option>
            <option value="forest">Forest</option>
            <option value="sunset">Sunset</option>
            <option value="purple">Purple</option>
          </select></label>
        <label class="setting"><span>Reading</span>
          <select id="layout">
            <option value="paged">One section</option>
            <option value="all">Whole document</option>
          </select></label>
      </div>
    </section>"""


def build(out_path: Path, quick: bool) -> int:
    runec = ROOT / "build" / "bin" / "runec"
    stdlib = ROOT / "stdlib"
    if not quick and not runec.exists():
        print(f"error: {runec} not found; build the project or pass --quick",
              file=sys.stderr)
        return 2

    workdir = Path(tempfile.mkdtemp(prefix="rune-docs-"))
    tools = Toolchain(runec=runec, stdlib=stdlib, workdir=workdir,
                      enabled=not quick)

    doc = content_module.DOC
    sections = doc["sections"]

    # ---- body -----------------------------------------------------------
    body_parts: list[str] = []
    rail_parts: list[str] = []
    sample_count = 0

    for idx, sec in enumerate(sections, start=1):
        sid = sec["id"]
        headings: list[tuple[str, str]] = []
        items_html: list[str] = []

        for j, item in enumerate(sec["items"]):
            if item["kind"] == "heading":
                hid = f"{sid}-{slugify(item['text'])}"
                headings.append((hid, item["text"]))
                items_html.append(
                    f'<h3 id="{hid}" data-title="{html.escape(item["text"])}">'
                    f'{inline(item["text"])}'
                    f'<a class="anchor" href="#{hid}" aria-label="Link to this heading">#</a></h3>'
                )
                continue
            if item["kind"] == "sample":
                sample_count += 1
            items_html.append(render_item(item, tools, f"{sid}_{j}"))

        prev = sections[idx - 2] if idx > 1 else None
        nxt = sections[idx] if idx < len(sections) else None
        pager = '<nav class="pager" aria-label="Sections">'
        if prev:
            pager += (
                f'<a class="prev" href="#{prev["id"]}" rel="prev">'
                f'<span class="dir">&larr; previous</span>'
                f'<span class="what">{inline(prev["title"])}</span></a>'
            )
        else:
            pager += '<span class="gap"></span>'
        pager += f'<span class="of">{idx} / {len(sections)}</span>'
        if nxt:
            pager += (
                f'<a class="next" href="#{nxt["id"]}" rel="next">'
                f'<span class="dir">next &rarr;</span>'
                f'<span class="what">{inline(nxt["title"])}</span></a>'
            )
        else:
            pager += '<span class="gap"></span>'
        pager += "</nav>"

        body_parts.append(
            f'<section id="{sid}">'
            f'<p class="eyebrow">§{idx} &nbsp;{html.escape(sec["kicker"])}</p>'
            f'<h2>{inline(sec["title"])}</h2>'
            f'<p class="blurb">{inline(sec["blurb"])}</p>'
            + "".join(items_html)
            + pager
            + "</section>"
        )

        search_terms = " ".join(
            [sec["title"], sec["kicker"], sec["blurb"]]
            + [t for _, t in headings]
            + sec.get("keywords", [])
        )
        subs = "".join(
            f'<li><a href="#{hid}">{inline(text)}</a></li>' for hid, text in headings
        )
        rail_parts.append(
            f'<li class="rail-group" data-for="{sid}" '
            f'data-title="{html.escape(sec["title"])}" '
            f'data-search="{html.escape(search_terms)}">'
            f'<a href="#{sid}"><span class="n">{idx:02d}</span>'
            f'<span>{inline(sec["title"])}</span></a>'
            f'<ol class="rail-sub">{subs}</ol></li>'
        )

    if tools.failures:
        print(f"\n{len(tools.failures)} sample(s) failed verification:\n",
              file=sys.stderr)
        for f in tools.failures:
            print("  " + f.replace("\n", "\n  "), file=sys.stderr)
            print(file=sys.stderr)
        shutil.rmtree(workdir, ignore_errors=True)
        return 1

    verified = "not checked" if quick else f"{tools.compiled}"

    page = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Rune Language Reference</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="description" content="The complete reference for Rune: every construct, with examples verified against the compiler.">
<script>{BOOT}</script>
<style>{CSS}</style>
<noscript><style>.copy{{display:none}}.settings{{display:none}}</style></noscript>
</head>
<body>

<div class="wrap">
  <aside class="rail">
    <div class="railhead">
      <span class="mark">R</span>
      <span class="who"><b>Rune</b><span>{html.escape(doc["version"])}</span></span>
    </div>
    <div class="search">
      <input id="q" type="search" placeholder="Filter  ( / )" aria-label="Filter sections">
    </div>
    <nav aria-label="Contents">
      <ol>{"".join(rail_parts)}</ol>
      <p class="nores" id="nores">Nothing matches that.</p>
    </nav>
  </aside>

  <main>{"".join(body_parts)}
    <footer>
      <div class="footer-inner">
        <span>{inline(doc["footer"])}</span>
        <span class="counts">{len(sections)} sections &middot; {sample_count} examples
        &middot; {verified} compiler-checked &middot; &larr; &rarr; to page</span>
      </div>
    </footer>
  </main>

  <aside class="toc">
    {SETTINGS}
    <nav class="tocnav" aria-label="On this page">
      <h4>On this page</h4>
      <ol id="toclist"></ol>
    </nav>
  </aside>

</div>
<script>{JS}</script>
</body>
</html>
"""

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(page)
    shutil.rmtree(workdir, ignore_errors=True)
    size_kb = len(page) / 1024
    print(f"wrote {out_path} ({size_kb:.0f} KB)")
    print(f"  {len(sections)} sections, {sample_count} samples, "
          f"{tools.compiled} compiled")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "docs" / "rune-reference.html"))
    ap.add_argument("--quick", action="store_true",
                    help="skip compiler verification")
    args = ap.parse_args()
    return build(Path(args.out), args.quick)


if __name__ == "__main__":
    raise SystemExit(main())
