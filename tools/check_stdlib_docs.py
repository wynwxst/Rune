#!/usr/bin/env python3
"""Compiles and runs every ```rune program in stdlib/docs/**.

The standard library's guide pages are read by `rune doc std::<module>`, and
a sample that no longer compiles is worse than no sample: this is what keeps
them honest, the way docs/reference/build.py keeps the reference honest.

    python3 tools/check_stdlib_docs.py            # uses build-release/bin/runec
    python3 tools/check_stdlib_docs.py --runec build/bin/runec

A fenced block is checked when it contains `fn main`; a fragment is left
alone. Programs run with no input and must exit 0 without a leak report.
"""
import argparse, glob, os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--runec", default=os.path.join(ROOT, "build-release", "bin", "runec"))
    ap.add_argument("--stdlib", default=os.path.join(ROOT, "stdlib"))
    ap.add_argument("pages", nargs="*")
    args = ap.parse_args()
    if not os.path.exists(args.runec):
        alt = os.path.join(ROOT, "build", "bin", "runec")
        if os.path.exists(alt):
            args.runec = alt
    pages = args.pages or sorted(glob.glob(os.path.join(args.stdlib, "docs", "**", "*.md"), recursive=True))
    total = bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        for page in pages:
            text = open(page, encoding="utf-8").read()
            for i, m in enumerate(re.finditer(r"```rune\n(.*?)```", text, re.S)):
                code = m.group(1)
                if "fn main" not in code:
                    continue
                total += 1
                rel = os.path.relpath(page, os.path.join(args.stdlib, "docs"))
                name = rel.replace(os.sep, "_").replace(".md", "") + f"_{i}"
                src = os.path.join(tmp, name + ".rune")
                exe = os.path.join(tmp, name)
                open(src, "w", encoding="utf-8").write(code)
                r = subprocess.run([args.runec, "--stdlib", args.stdlib, "-o", exe, src],
                                   capture_output=True, text=True)
                if r.returncode:
                    bad += 1
                    print(f"COMPILE FAIL {rel} sample {i}:\n" + "\n".join(r.stderr.strip().split("\n")[:10]))
                    continue
                try:
                    run = subprocess.run([exe], capture_output=True, text=True, timeout=60,
                                         stdin=subprocess.DEVNULL)
                except subprocess.TimeoutExpired:
                    bad += 1
                    print(f"TIMEOUT {rel} sample {i}")
                    continue
                if run.returncode != 0:
                    bad += 1
                    print(f"RUN FAIL {rel} sample {i} (exit {run.returncode}):\n{run.stderr[-600:]}")
                elif "still live at exit" in run.stderr:
                    bad += 1
                    print(f"LEAK {rel} sample {i}")
    print(f"{total - bad}/{total} stdlib doc samples ok")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main())
