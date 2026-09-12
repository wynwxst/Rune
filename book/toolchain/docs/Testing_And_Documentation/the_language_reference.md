# The language reference

The reference is **generated**. `docs/docs/**` and `docs/rune-reference.html`
are both build outputs — editing either by hand works until the next
regeneration deletes it.

The single source is `docs/reference/content.py`.

## Changing it

```sh
# 1. edit the content
$EDITOR docs/reference/content.py

# 2. regenerate the pages
python3 docs/reference/export_markdown.py

# 3. build the HTML
rune doc -C docs
```

Step 2 writes `docs/docs/**`, one folder per section and one file per heading,
preserving `docs/docs/target/` (which step 3 writes). Step 3 turns those pages
into `docs/docs/target/index.html`.

The standalone single-page reference is separate and slower, because it
compiles every sample:

```sh
python3 docs/reference/build.py           # docs/rune-reference.html
python3 docs/reference/build.py --quick   # skip verification, for layout work
```

## The shape of `content.py`

A list of `Sec(...)`, each holding items built by small helpers:

| Helper | Is |
| --- | --- |
| `H("Heading")` | A heading; also the boundary that becomes one page |
| `P("prose")` | A paragraph |
| `S(code, mode=...)` | A Rune sample |
| `T(headers, rows)` | A table |
| `N(text, label=...)` | A callout |
| `G(text)` | Grammar |
| `SH(text)` | Shell or terminal text |

```python
SECTIONS.append(Sec(
    "toolchain", "tooling", "The toolchain",
    "`runec` compiles files. `rune` manages packages and calls `runec`.",
    [
        H("runec"),
        T(["Flag", "Does"], [["`-o <path>`", "output path"]]),
        P("..."),
    ],
    keywords=["build", "compile", "flags"]))
```

## Sample modes, and why they matter

Every sample is handed to the real compiler when the standalone reference is
built, and the output shown beside it is what the program actually printed.
Nothing is transcribed by hand.

| `mode` | Means |
| --- | --- |
| `run` | A whole program: compiled, linked, executed, stdout captured |
| `decls` | Declarations only — a trivial `main` is appended so it still compiles |
| `diag` | Expected to be rejected; the compiler's own diagnostic is captured |
| `panic` | Expected to abort; the panic and traceback are captured |
| `frag` | Grammar or shell text, not Rune, so not compiled |

This is why the reference doubles as a test: a sample that stops working is a
failed build, not a stale page. It is also why `mode="frag"` should be used
sparingly — a fragment is a claim nothing checks.

## Where a new page appears

Pages come from `H(...)` headings inside a section, and folders from section
titles, so a new heading is a new page with no other bookkeeping. Filenames are
slugged from the heading — `"How a build decides what to do"` becomes
`how_a_build_decides_what_to_do.md` — so renaming a heading renames its file.
