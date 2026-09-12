#!/usr/bin/env python3
"""Export docs/reference/content.py into docs/markdown/<Category>/<heading>.md."""

from __future__ import annotations

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import content as content_module  # noqa: E402

# Where `rune doc` looks: the docs/ folder of the package rooted at docs/.
# The two have to be the same place, or `rune doc` would go on rendering the
# previous export and a change to content.py would never reach the pages.
OUT = ROOT / "docs" / "docs"

# Everything under OUT is generated except these. `target` is where `rune doc`
# used to put its output; `order.json` is regenerated below, and is listed here
# so that a run which stops early leaves the reading order intact rather than
# dropping the whole reference into alphabetical order.
KEEP = {"target", "order.json"}


def folder_name(title: str) -> str:
    words = re.findall(r"[A-Za-z0-9]+", title.replace("`", ""))
    return "_".join(w[0].upper() + w[1:] for w in words) or "Section"


def file_slug(title: str) -> str:
    t = title.replace("`", "")
    # Standalone operators keep a readable word; `std::io` stays a separator.
    t = re.sub(r"(?<![A-Za-z0-9])::(?![A-Za-z0-9])", " coloncolon ", t)
    t = t.replace("::", " ")
    t = re.sub(r"(?<![A-Za-z0-9])\.(?![A-Za-z0-9])", " dot ", t)
    t = t.replace(".", " ")
    t = re.sub(r"(?<![A-Za-z0-9])\?(?![A-Za-z0-9])", " question ", t)
    t = t.replace("?", " ")
    t = re.sub(r"(?<![A-Za-z0-9])\$(?![A-Za-z0-9])", " dollar ", t)
    t = t.replace("@", " ")
    t = t.replace("{ }", " block ").replace("{}", " block ")
    words = re.findall(r"[A-Za-z0-9]+", t)
    return "_".join(w.lower() for w in words) or "section"


def unique_slug(title: str, used: dict[str, int]) -> str:
    slug = file_slug(title)
    used[slug] += 1
    if used[slug] > 1:
        slug = f"{slug}_{used[slug]}"
    return slug


def fence(lang: str, body: str) -> str:
    text = body.strip("\n")
    ticks = "```"
    while ticks in text:
        ticks += "`"
    opener = f"{ticks}{lang}" if lang else ticks
    return f"{opener}\n{text}\n{ticks}"


def sample_lang(item: dict) -> str:
    if item.get("mode") != "frag":
        return "rune"
    title = (item.get("title") or "").lower()
    code = item.get("code") or ""
    if "toml" in title or "rune.toml" in title:
        return "toml"
    if "└" in code or "──" in code:
        return "text"
    return "text"


def md_cell(text: str) -> str:
    # content.py sometimes markdown-escapes pipes already; don't double them.
    text = text.replace("\n", " ").replace("\\|", "|").replace("|", "\\|")
    return text


def render_table(item: dict) -> str:
    headers = item["headers"]
    rows = item["rows"]
    head = "| " + " | ".join(md_cell(h) for h in headers) + " |"
    sep = "| " + " | ".join("---" for _ in headers) + " |"
    body = "\n".join(
        "| " + " | ".join(md_cell(c) for c in row) + " |" for row in rows
    )
    out = f"{head}\n{sep}\n{body}"
    caption = item.get("caption")
    if caption:
        out += f"\n\n*{caption}*"
    return out


def render_note(item: dict) -> str:
    label = item.get("label", "Note")
    tone = item.get("tone", "note")
    kind = "WARNING" if tone == "warn" else "NOTE"
    text = item["text"].strip()
    lines = text.split("\n")
    body = "\n".join(f"> {line}" if line else ">" for line in lines)
    return f"> [!{kind}]\n> **{label}**\n>\n{body}"


def render_sample(item: dict) -> str:
    parts: list[str] = []
    title = item.get("title")
    if title:
        parts.append(f"**{title}**")
        parts.append("")
    parts.append(fence(sample_lang(item), item["code"]))
    return "\n".join(parts)


def render_item(item: dict) -> str:
    kind = item["kind"]
    if kind == "prose":
        return item["text"].strip()
    if kind == "note":
        return render_note(item)
    if kind == "table":
        return render_table(item)
    if kind == "grammar":
        return fence("ebnf", item["text"])
    if kind == "shell":
        return fence("sh", item["text"])
    if kind == "sample":
        return render_sample(item)
    raise ValueError(f"unknown item kind {kind!r}")


def render_items(items: list[dict]) -> str:
    chunks = [render_item(item) for item in items]
    return "\n\n".join(chunk for chunk in chunks if chunk).rstrip() + "\n"


def split_sections(items: list[dict]) -> tuple[list[dict], list[tuple[str, list[dict]]]]:
    preamble: list[dict] = []
    groups: list[tuple[str, list[dict]]] = []
    current: list[dict] | None = None
    heading: str | None = None
    for item in items:
        if item["kind"] == "heading":
            if heading is not None and current is not None:
                groups.append((heading, current))
            heading = item["text"]
            current = []
            continue
        if current is None:
            preamble.append(item)
        else:
            current.append(item)
    if heading is not None and current is not None:
        groups.append((heading, current))
    return preamble, groups


HOW_TO_READ = """# How to read this reference

This tree is the language reference split by category and heading. Every code sample comes from the same source as the HTML reference (`docs/reference/content.py`): complete programs that compile and run, declarations type-checked with a trivial `main` appended, and fragments that are grammar, shell, or manifest text rather than Rune.

| In this tree | Is |
| --- | --- |
| a folder | one category |
| a markdown file | one heading in that category |
| a caption above a code block | what the sample is showing |
| a `rune` fence | Rune source |
| a `sh` fence | a shell command |
| an `ebnf` fence | grammar |
| a `[!NOTE]` / `[!WARNING]` callout | a labelled aside |

The HTML build in `docs/reference/build.py` still compiles every sample against `runec` when it generates `docs/rune-reference.html`.
"""


def main() -> int:
    if OUT.exists():
        stale = [p for p in OUT.iterdir() if p.name not in KEEP]
        for top in stale:
            if top.is_file():
                top.unlink()
                continue
            for path in top.rglob("*"):
                if path.is_file():
                    path.unlink()
            for path in sorted(top.rglob("*"), reverse=True):
                if path.is_dir():
                    path.rmdir()
            top.rmdir()
    OUT.mkdir(parents=True, exist_ok=True)

    doc = content_module.DOC
    index_lines = [
        f"# Rune language reference",
        "",
        doc["tagline"],
        "",
        f"Version `{doc['version']}`. One folder per category, one file per heading.",
        "",
    ]

    file_count = 0
    order: dict[str, object] = {"folders": []}
    for sec in doc["sections"]:
        folder = folder_name(sec["title"])
        dest = OUT / folder
        dest.mkdir(parents=True, exist_ok=True)
        preamble, groups = split_sections(sec["items"])
        used: dict[str, int] = defaultdict(int)
        pages: list[tuple[str, str]] = []

        for heading, items in groups:
            slug = unique_slug(heading, used)
            pages.append((heading, f"{slug}.md"))
            body = f"# {heading}\n\n" + render_items(items)
            if heading == "How to read this reference":
                body = HOW_TO_READ
            (dest / f"{slug}.md").write_text(body)
            file_count += 1

        readme_parts = [f"# {sec['title']}", "", sec["blurb"].strip()]
        if preamble:
            readme_parts += ["", render_items(preamble).rstrip()]
        if pages:
            readme_parts += ["", "## Pages", ""]
            for heading, filename in pages:
                readme_parts.append(f"- [{heading}]({filename})")
            readme_parts.append("")
        (dest / "README.md").write_text("\n".join(readme_parts).rstrip() + "\n")
        file_count += 1

        # A folder's own page comes first wherever `rune doc` finds one, so
        # only the rest needs naming here.
        order["folders"].append(folder)
        order[folder] = [filename for _, filename in pages]

        index_lines.append(f"## {sec['title']}")
        index_lines.append("")
        index_lines.append(sec["blurb"].strip())
        index_lines.append("")
        for heading, filename in pages:
            index_lines.append(f"- [{heading}]({folder}/{filename})")
        index_lines.append("")

    (OUT / "README.md").write_text("\n".join(index_lines).rstrip() + "\n")
    file_count += 1

    # `rune doc` reads this to know what order to present the pages in.
    # Without it the reference is alphabetical, which puts `Any` before
    # `Getting started` and the appendix in the middle.
    #
    # Generated rather than kept by hand: content.py already holds the order —
    # sections in the order they are appended, pages in the order their
    # headings appear — so writing it out here is the only way it cannot drift
    # from what it is describing.
    (OUT / "order.json").write_text(json.dumps(order, indent=1) + "\n")
    file_count += 1

    print(f"wrote {file_count} files under {OUT}")
    print(f"  {len(doc['sections'])} categories, order.json written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
