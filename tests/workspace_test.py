#!/usr/bin/env python3
"""Workspaces and local units, end to end, through `rune`.

A workspace is made with `rune ws new`, given a library and a binary that
depends on it by path, and built from its root: the library goes first, the
binary runs, `rune ws list` shows the order, `remove` and `add` edit the
manifest without touching files, and `rune ws init` finds packages already
there. A member that fails stops the run and says which.

Invoked by CTest as `rune_workspace`; run it by hand with

    python3 tests/workspace_test.py build/bin
"""
import os, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/bin")
RUNE = os.path.join(BIN, "rune")
failures = []


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))


def rune(*args, cwd):
    p = subprocess.run([RUNE, *args], cwd=cwd, capture_output=True, text=True,
                       timeout=600)
    return p.returncode, p.stdout + p.stderr


APP = """import std::io
import geometry

fn main() -> i64 {
    io::println(geometry::greeting() + ": " + geometry::add(2, 3).$str())
    0
}
"""

tmp = tempfile.mkdtemp(prefix="rune-ws-")
try:
    rc, out = rune("ws", "new", "studio", cwd=tmp)
    root = os.path.join(tmp, "studio")
    check("ws new makes a workspace", rc == 0 and os.path.exists(
        os.path.join(root, "Rune.toml")), out)

    rc, out = rune("ws", "add", "app", cwd=root)
    check("ws add makes a binary package", rc == 0 and os.path.exists(
        os.path.join(root, "app", "src", "main.rune")), out)
    rc, out = rune("ws", "add", "geometry", "--lib", cwd=root)
    check("ws add --lib makes a library", rc == 0 and os.path.exists(
        os.path.join(root, "geometry", "src", "lib.rune")), out)
    rc, out = rune("ws", "add", "app", cwd=root)
    check("adding a member twice is refused", rc != 0 and "already" in out, out)

    manifest = open(os.path.join(root, "app", "Rune.toml")).read()
    manifest = manifest.replace("[dependencies]",
                                '[dependencies]\ngeometry = { path = "../geometry" }')
    open(os.path.join(root, "app", "Rune.toml"), "w").write(manifest)
    open(os.path.join(root, "app", "src", "main.rune"), "w").write(APP)

    rc, out = rune("ws", "list", cwd=os.path.join(root, "app"))
    lines = [l.strip() for l in out.splitlines() if l.startswith("  ")]
    check("ws list, from inside a member, puts the library first",
          rc == 0 and len(lines) == 2 and lines[0].startswith("geometry")
          and "uses geometry" in lines[1], out)

    rc, out = rune("build", cwd=root)
    check("rune build at the root builds every member", rc == 0 and
          "done in 2 member(s)" in out and
          out.find("geometry") < out.find("Member app"), out)

    rc, out = rune("ws", "run", "app", cwd=root)
    check("ws run runs a member", rc == 0 and "hello from geometry: 5" in out, out)

    rc, out = rune("ws", "remove", "geometry", cwd=root)
    text = open(os.path.join(root, "Rune.toml")).read()
    check("ws remove edits only the manifest",
          rc == 0 and '"geometry"' not in text and
          os.path.isdir(os.path.join(root, "geometry")), out + text)
    rc, out = rune("ws", "add", "geometry", cwd=root)
    check("ws add takes an existing package back", rc == 0, out)

    open(os.path.join(root, "geometry", "src", "lib.rune"), "a").write(
        "\npub fn broken() -> i64 { \"no\" }\n")
    rc, out = rune("ws", "check", cwd=root)
    check("a failing member stops the run and is named",
          rc != 0 and "failed in member 'geometry'" in out, out)

    other = os.path.join(tmp, "other")
    os.makedirs(os.path.join(other, "libs"))
    shutil.copytree(os.path.join(root, "app"), os.path.join(other, "app"))
    rc, out = rune("new", "util", "--lib", cwd=os.path.join(other, "libs"))
    rc, out = rune("ws", "init", cwd=other)
    text = open(os.path.join(other, "Rune.toml")).read()
    check("ws init finds the packages already there",
          rc == 0 and '"app"' in text and '"libs/util"' in text, out + text)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print("\n%d failure(s):" % len(failures))
    for f in failures:
        print("  " + f.replace("\n", "\n    "))
    sys.exit(1)
print("all workspace checks passed")
