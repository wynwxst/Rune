#!/usr/bin/env python3
"""Checks every code sample in the book against the real compiler.

    python3 check.py                 # every page, every project
    python3 check.py docs/Ownership  # one chapter (or one .md file)
    python3 check.py --projects      # only the example packages
    python3 check.py --no-projects   # only the pages
    python3 check.py --fix PAGE      # rewrite `output` fences with real output

Fence conventions (the text after ```):

    rune                a complete program (has `fn main`). Compiled and run.
                        If an ```output fence follows, stdout must match it.
    rune,fails          must NOT compile. If an ```error fence follows, every
                        line of it must appear in the compiler's output.
    rune,panics         compiles; running it must exit non-zero. An ```output
                        fence that follows is matched against stdout + stderr
                        (every line of the fence must appear).
    rune,arc            same as `rune`, compiled with --memory arc
    rune,file=PATH      an excerpt of examples/PATH; must appear in that file
    rune,ignore         shown, not checked
    sh / bash / toml    not checked

A ```stdin fence directly after a program feeds it input; an ```output fence
may follow that.
"""
import concurrent.futures as cf
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.abspath(__file__))
DOCS = os.path.join(ROOT, "docs")
EXAMPLES = os.path.join(ROOT, "examples")
ANSI = re.compile(r"\x1b\[[0-9;]*m")
FENCE = re.compile(r"^```([^\n]*)\n(.*?)^```[ \t]*$", re.S | re.M)


def blocks(path):
    text = open(path, encoding="utf-8").read()
    out = []
    for m in FENCE.finditer(text):
        line = text.count("\n", 0, m.start()) + 1
        out.append((m.group(1).strip(), m.group(2), line))
    return out


def norm(s):
    return "\n".join(l.rstrip() for l in s.strip().splitlines())


def compile_run(code, memory, stdin, tmp, name):
    src = os.path.join(tmp, name + ".rune")
    exe = os.path.join(tmp, name)
    open(src, "w", encoding="utf-8").write(code)
    cmd = ["runec", src, "-o", exe]
    if memory:
        cmd += ["--memory", memory]
    c = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if c.returncode != 0:
        return c, None
    try:
        r = subprocess.run([exe], input=stdin or "", capture_output=True,
                           text=True, timeout=30, cwd=tmp)
    except subprocess.TimeoutExpired:
        return c, None
    r.stdout = ANSI.sub("", r.stdout)
    r.stderr = ANSI.sub("", r.stderr)
    return c, r


def check_block(job):
    page, line, info, code, expect, stdin = job
    kind = info.split(",")
    errors = []
    actual = None
    with tempfile.TemporaryDirectory() as tmp:
        memory = "arc" if "arc" in kind else None
        c, r = compile_run(code, memory, stdin, tmp, "t")
        if "fails" in kind:
            if c.returncode == 0:
                errors.append("expected a compile error, but it compiled")
            elif expect is not None:
                for l in expect.strip().splitlines():
                    if l.strip() and l.strip() not in c.stderr + c.stdout:
                        errors.append("missing from compiler output: " + l.strip())
        else:
            if c.returncode != 0:
                errors.append("does not compile:\n" + (c.stderr or c.stdout)[:1500])
            elif r is None:
                errors.append("timed out")
            elif "panics" in kind:
                if r.returncode == 0:
                    errors.append("expected a non-zero exit")
                elif expect is not None:
                    got = r.stdout + r.stderr
                    for l in expect.strip().splitlines():
                        if l.strip() and l.strip() not in got:
                            errors.append("missing from output: " + l.strip())
            elif expect is not None:
                actual = r.stdout
                if norm(r.stdout) != norm(expect):
                    errors.append("output differs.\n--- book\n%s\n--- actual\n%s"
                                  % (norm(expect), norm(r.stdout)))
    return page, line, errors, actual


def page_jobs(path):
    jobs, errors = [], []
    bl = blocks(path)
    rel = os.path.relpath(path, ROOT)
    for i, (info, code, line) in enumerate(bl):
        kind = info.split(",")
        if kind[0] != "rune":
            continue
        if "ignore" in kind:
            continue
        file_attr = [k for k in kind if k.startswith("file=")]
        if file_attr:
            target = os.path.join(EXAMPLES, file_attr[0][5:])
            if not os.path.exists(target):
                errors.append((rel, line, ["no such file: " + target]))
                continue
            have = norm(open(target, encoding="utf-8").read())
            want = norm(code)
            if want not in have:
                errors.append((rel, line, ["excerpt is not in " + file_attr[0][5:]]))
            continue
        if "fn main" not in code:
            errors.append((rel, line, ["fence has no `fn main`: mark it "
                                       "`rune,ignore` or make it a program"]))
            continue
        expect = None
        want_kind = "error" if "fails" in kind else "output"
        stdin = None
        j = i + 1
        if j < len(bl) and bl[j][0] == "stdin":
            stdin = bl[j][1]
            j += 1
        if j < len(bl) and bl[j][0] == want_kind:
            expect = bl[j][1]
        jobs.append((rel, line, info, code, expect, stdin))
    return jobs, errors


def check_packages():
    errs = []
    if not os.path.isdir(EXAMPLES):
        return errs
    skip = {"rugrep", "guessing_game"}   # earlier drafts, not part of the new book
    pkgs = sorted(d for d in os.listdir(EXAMPLES)
                  if d not in skip and os.path.exists(os.path.join(EXAMPLES, d, "Rune.toml")))

    def one(d):
        p = os.path.join(EXAMPLES, d)
        out = []
        b = subprocess.run(["rune", "build"], cwd=p, capture_output=True, text=True)
        if b.returncode != 0:
            out.append("examples/%s: build failed\n%s" % (d, (b.stdout + b.stderr)[-1500:]))
            return out
        if os.path.isdir(os.path.join(p, "tests")):
            t = subprocess.run(["rune", "test"], cwd=p, capture_output=True, text=True)
            if t.returncode != 0:
                out.append("examples/%s: tests failed\n%s" % (d, (t.stdout + t.stderr)[-1500:]))
        return out

    with cf.ThreadPoolExecutor(max_workers=4) as ex:
        for r in ex.map(one, pkgs):
            errs += r
    print("checked %d packages" % len(pkgs))
    return errs


def main():
    args = sys.argv[1:]
    do_pages = "--projects" not in args
    do_projects = "--no-projects" not in args
    paths = [a for a in args if not a.startswith("--")]
    mds = []
    for base in (paths or [DOCS]):
        base = os.path.abspath(base)
        if os.path.isfile(base):
            mds.append(base)
        else:
            for d, _, fs in os.walk(base):
                mds += [os.path.join(d, f) for f in fs if f.endswith(".md")]
    mds.sort()
    failures = []
    fixes = {}
    fix = "--fix" in args
    if do_pages:
        jobs = []
        for md in mds:
            j, e = page_jobs(md)
            jobs += j
            failures += [("%s:%d" % (p, l), es) for p, l, es in e]
        with cf.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
            for (job, (page, line, es, actual)) in zip(jobs, ex.map(check_block, jobs)):
                if es and fix and actual is not None and not any(
                        e.startswith("does not") or e.startswith("timed") for e in es):
                    fixes.setdefault(page, []).append((job, actual))
                    es = []
                if es:
                    failures.append(("%s:%d" % (page, line), es))
        print("checked %d programs in %d pages" % (len(jobs), len(mds)))
    for page, items in fixes.items():
        path = os.path.join(ROOT, page)
        text = open(path, encoding="utf-8").read()
        for job, actual in items:
            old = job[4]
            new = actual.rstrip("\n") + "\n"
            text = text.replace("```output\n" + old + "```", "```output\n" + new + "```", 1)
        open(path, "w", encoding="utf-8").write(text)
        print("fixed outputs in", page)
    pk = []
    if do_projects and not paths:
        pk = check_packages()
    for where, es in failures:
        print("\nFAIL %s" % where)
        for e in es:
            print("  " + e.replace("\n", "\n  "))
    for e in pk:
        print("\nFAIL " + e)
    if failures or pk:
        sys.exit(1)
    print("all good")


if __name__ == "__main__":
    main()
