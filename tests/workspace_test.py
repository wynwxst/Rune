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

    # Local units: folders under src/, each compiled on its own.
    rc, out = rune("new", "units", cwd=tmp)
    pkg = os.path.join(tmp, "units")
    os.makedirs(os.path.join(pkg, "src", "unit"))
    os.makedirs(os.path.join(pkg, "src", "unit2"))
    open(os.path.join(pkg, "src", "unit", "hello.rune"), "w").write(
        'pub fn hello() -> String { "hello" }\n')
    open(os.path.join(pkg, "src", "unit2", "bye.rune"), "w").write(
        'import unit::hello\npub fn bye() -> String { hello::hello() + " and bye" }\n')
    open(os.path.join(pkg, "src", "main.rune"), "w").write(
        "import std::io\nimport unit::hello\nimport unit2::bye\n"
        "fn main() -> i64 {\n    io::println(hello::hello())\n"
        "    io::println(bye::bye())\n    0\n}\n")
    rc, out = rune("build", cwd=pkg)
    check("units compile before the package, in import order",
          rc == 0 and out.find("units::unit (unit)") < out.find("units::unit2 (unit)")
          < out.find("Compiling units v"), out)
    rc, out = rune("run", cwd=pkg)
    check("main imports unit::hello and unit2::bye",
          rc == 0 and "hello\nhello and bye" in out, out)
    rc, out = rune("build", cwd=pkg)
    check("a second build compiles nothing", rc == 0 and "Compiling" not in out, out)
    with open(os.path.join(pkg, "src", "unit2", "bye.rune"), "a") as f:
        f.write("// edited\n")
    rc, out = rune("build", cwd=pkg)
    check("editing one unit leaves the units it imports alone",
          rc == 0 and "unit2 (unit)" in out and "units::unit (unit)" not in out, out)
    open(os.path.join(pkg, "src", "unit", "loop.rune"), "w").write(
        "import unit2::bye\npub fn x() -> String { bye::bye() }\n")
    rc, out = rune("build", cwd=pkg)
    check("units importing one another in a circle are named",
          rc != 0 and "in a circle" in out, out)
    os.remove(os.path.join(pkg, "src", "unit", "loop.rune"))
    os.makedirs(os.path.join(pkg, "src", "bad-name"))
    open(os.path.join(pkg, "src", "bad-name", "x.rune"), "w").write("pub fn x() {}\n")
    rc, out = rune("build", cwd=pkg)
    check("a folder that is not a name cannot be a unit",
          rc != 0 and "is not a name" in out, out)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print("\n%d failure(s):" % len(failures))
    for f in failures:
        print("  " + f.replace("\n", "\n    "))
    sys.exit(1)
print("all workspace checks passed")
