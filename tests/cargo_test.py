#!/usr/bin/env python3
"""Rust crates as dependencies, end to end, through `rune`.

`name = { cargo = "path" }` in `[dependencies]` has Cargo build the crate as
a static library and binds what it exports over the C ABI into the Rune
module `name`. Two packages are built from copies, so nothing is written into
the source tree:

- tests/cargo/tricky, a crate full of syntax the binding reader must step
  around, and items it must leave out with the right reason;
- examples/rust_interop, structs, enums, callbacks, strings and an opaque
  handle, as a user would write them.

`rune ffi rust` must write the same module on demand. Then a build with
nothing changed must not start Cargo, a change to the crate must rebuild it
and its bindings, and a missing crate must say so.

Invoked by CTest as `rune_cargo` when `cargo` is on PATH; run it by hand with

    python3 tests/cargo_test.py build/bin
"""
import os, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/bin")
RUNE = os.path.join(BIN, "rune")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
failures = []


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))


def rune(*args, cwd):
    p = subprocess.run([RUNE, *args], cwd=cwd, capture_output=True, text=True,
                       timeout=900)
    return p.returncode, p.stdout + p.stderr


def copy(src, tmp):
    dst = os.path.join(tmp, os.path.basename(src))
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("target"))
    return dst


if not shutil.which("cargo"):
    print("skip: cargo is not on PATH")
    sys.exit(0)

tmp = tempfile.mkdtemp(prefix="rune-cargo-")
try:
    tricky = copy(os.path.join(ROOT, "tests", "cargo", "tricky"), tmp)
    rc, out = rune("run", cwd=tricky)
    check("the tricky crate builds and runs", rc == 0, out)
    for line in ["2 10", "5 15", "8 1.75", "10 4",
                 "4294967295 -42 170 2.5 true", "77 true"]:
        check("tricky prints " + line, line in out.splitlines(), out)
    bound = open(os.path.join(tricky, "target", "debug", "cargo", "odd.rune")).read()
    for reason in ["Shaped: an enum with data", "Bits: a union is not bound",
                   "generic_one: a generic function",
                   "Pair: a tuple struct", "Generic: a generic struct",
                   "HasString.name: 'String' has no C layout",
                   "takes_slice: a slice has no C layout",
                   "takes_rust_fn: a Rust-ABI function pointer",
                   "Wrapped: `#[repr(transparent)]`",
                   "Twice: declared 2 times in different modules"]:
        check("left out: " + reason.split(":")[0], reason in bound, bound)
    # Not items, or not the crate's API: strings and raw strings that look
    # like items, a test module, an import, private, module-level and
    # associated constants.
    for absent in ["fake", "nope", "test_only", "fn abs(", "PRIVATE", "TRICK", "RAW",
                   "HIDDEN", "ASSOCIATED"]:
        check("not read as an item: " + absent, absent not in bound, bound)
    for present in ["fn never_returns() -> Never", "fn label(_c: CString) -> i32",
                    "pub type Opaque = u8", "#as(\"__rust_rust_side_name\")",
                    "fn renamed_symbol(x: u64) -> u64", "pub tag: [4:u8]",
                    "@cfunction(*var u8, i32)", "edition_2024(type_: u32, in_: u32)"]:
        check("bound as " + present, present in bound, bound)

    # `rune ffi rust`: the same module, written on demand.
    p = subprocess.run([RUNE, "ffi", "rust", "rust"], cwd=tricky,
                       capture_output=True, text=True, timeout=120)
    check("ffi rust prints the module the build wrote",
          p.returncode == 0 and p.stdout == bound, p.stdout + p.stderr)
    check("ffi rust says what it bound on stderr",
          "bound 13 functions from 'odd_crate'" in p.stderr and "11 items left out" in p.stderr,
          p.stderr)
    written = os.path.join(tmp, "out", "odd.rune")
    rc, out = rune("ffi", "rust", os.path.join(tricky, "rust"), "-o", written,
                   "--target", "x86_64-pc-windows-gnu", cwd=tmp)
    text = open(written).read() if os.path.exists(written) else ""
    check("ffi rust -o --target writes the file, c_long as 32 bits",
          rc == 0 and "pub count: i32" in text, out + text)
    rc, out = rune("ffi", "rust", os.path.join(tmp, "nowhere"), cwd=tmp)
    check("ffi rust on a directory without a crate fails",
          rc != 0 and "no Cargo.toml" in out, out)

    rc, out = rune("build", cwd=tricky)
    check("nothing changed: Cargo is not run", rc == 0 and "Rust crate" not in out
          and "Binding" not in out, out)
    lib = os.path.join(tricky, "rust", "src", "lib.rs")
    with open(lib, "a") as f:
        f.write('\n#[no_mangle]\npub extern "C" fn added_later(x: i32) -> i32 { x * 2 }\n')
    rc, out = rune("build", cwd=tricky)
    check("a changed crate is rebuilt and bound again",
          rc == 0 and "Rust crate" in out and "Binding odd (14 functions)" in out, out)

    example = copy(os.path.join(ROOT, "examples", "rust_interop"), tmp)
    rc, out = rune("run", cwd=example)
    check("examples/rust_interop runs", rc == 0, out)
    for line in ["add: 42", "distance: 5.0", "area: 12.0", "turns left: true",
                 "geometry 64 3.141592653589793", "sum of squares: 14",
                 "centroid: 3.0, 2.0", "bytes: 6",
                 "hello from Rust! hello from Rust!", "polygon area: 12.0"]:
        check("rust_interop prints " + line, line in out.splitlines(), out)
    check("no linker warnings", "warning" not in out, out)

    manifest = os.path.join(example, "Rune.toml")
    text = open(manifest).read()
    open(manifest, "w").write(text.replace('"rust/geometry"', '"rust/nowhere"'))
    rc, out = rune("build", cwd=example)
    check("a missing crate is reported", rc != 0 and "no Cargo.toml" in out, out)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

if failures:
    print("\n%d failure(s):" % len(failures))
    for f in failures:
        print(" - " + f)
    sys.exit(1)
