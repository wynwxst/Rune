#!/usr/bin/env python3
"""Cross targets, driven through `rune` itself: resolving foreign targets and
`[target.<name>]` tables, and — when the WASI SDK and wasmtime are installed —
building and running WebAssembly.

Runs with an isolated RUNE_HOME in a temporary directory, so the runtimes it
builds for each target never touch the real cache. Invoked by CTest as
`rune_cross`; run it by hand with

    python3 tests/cross_test.py build/bin

The WebAssembly half is skipped, with a line saying so, on a machine without
the SDK (`$WASI_SDK_PATH`, or `/opt/wasi-sdk`) or without wasmtime.
"""
import os, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/bin")
RUNE = os.path.join(BIN, "rune")
RUNEC = os.path.join(BIN, "runec")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

failures = []


def rune(args, cwd, env):
    r = subprocess.run([RUNE, *args, "--no-color"], cwd=cwd, env=env,
                       capture_output=True, text=True, timeout=600)
    return r.returncode, r.stdout + r.stderr


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


def expected_lines(case):
    with open(case) as f:
        return [l[len("// EXPECT:"):].lstrip(" ").rstrip("\n")
                for l in f if l.startswith("// EXPECT:")]


def case_flags(case):
    """The `runec` flags a case's header asks for, as RunCases.cmake reads
    them: `// SAFETY: <level>` and `// FLAGS: <args...>`."""
    flags = []
    with open(case) as f:
        for l in f:
            if l.startswith("// SAFETY:"):
                flags += ["--safety", l[len("// SAFETY:"):].strip()]
            elif l.startswith("// FLAGS:"):
                flags += l[len("// FLAGS:"):].split()
    return flags


def resolution(tmp, env):
    """What needs no toolchain: names, aliases, tables, and mistakes."""
    code, out = rune(["targets"], tmp, env)
    check("`rune targets` works outside a package", code == 0, out)
    for name in ["wasm", "wasm-threads", "windows", "linux-arm64"]:
        check(f"`rune targets` lists {name}", f"  {name} " in out, out)

    rune(["new", "app"], tmp, env)
    app = os.path.join(tmp, "app")

    code, out = rune(["build", "--target", "wams"], app, env)
    check("a mistyped target fails", code != 0, out)
    check("a mistyped target suggests the right one",
          "did you mean 'wasm'?" in out, out)

    # A table with neither a triple nor a foreign target to stand on is
    # still a manifest error.
    manifest = os.path.join(app, "Rune.toml")
    original = open(manifest).read()
    write(manifest, original + '\n[target.nowhere]\ncc = "cc"\n')
    code, out = rune(["targets"], app, env)
    check("a table with no triple is refused",
          code != 0 and "[target.nowhere] has no `triple`" in out, out)

    write(manifest, original + '\n[target.odd]\nbase = "nonsense"\n')
    code, out = rune(["build", "--target", "odd"], app, env)
    check("an unknown base is named",
          code != 0 and "is not a foreign target" in out, out)

    # A foreign target whose SDK is pointed somewhere empty says so, rather
    # than falling back to the host's compiler.
    empty = os.path.join(tmp, "empty")
    os.makedirs(empty)
    write(manifest, original + f'\n[target.wasm]\nsdk = "{empty}"\n')
    code, out = rune(["build", "--target", "wasm"], app, env)
    check("a missing WASI SDK is reported",
          code != 0 and "no WASI SDK at" in out, out)
    write(manifest, original)


def raw_target(tmp, env):
    """A `[target.x]` with a triple of its own is raw: its runtime is built in
    the project, in target/x/runtime, with the table's `cc` and `c-flags` and
    nothing else — even for a triple with a runtime already cached — and the
    link carries only what the table says."""
    machine = subprocess.run(["cc", "-dumpmachine"], capture_output=True,
                             text=True).stdout.strip()
    if not machine:
        print("skip raw target: no `cc` here")
        return
    rune(["new", "rawpkg"], tmp, env)
    project = os.path.join(tmp, "rawpkg")
    manifest = os.path.join(project, "Rune.toml")
    with open(manifest, "a") as f:
        f.write('\n[target.x]\ntriple = "%s"\ncc = "cc"\n'
                'c-flags = ["-O1", "-DFROM_THE_TABLE"]\nlink-args = ["-lm"]\n'
                'runner = "env"\n' % machine)
    code, out = rune(["run", "--target", "x", "-v"], project, env)
    check("a raw target builds and runs", code == 0 and "Hello from rawpkg!" in out, out)
    check("its runtime is built in the project",
          os.path.isfile(os.path.join(project, "target", "x", "runtime",
                                      "libruneruntime.a")), out)
    compiles = [l.strip() for l in out.splitlines() if "-I" in l and " -c " in l]
    check("the runtime is compiled with the table's flags and nothing else",
          compiles and all(l.startswith("'cc' -c '-O1' '-DFROM_THE_TABLE' -I")
                           for l in compiles), "\n".join(compiles) or out)
    link = [l for l in out.splitlines() if "link: " in l]
    check("the link carries only what the table says",
          link and link[0].rstrip().endswith("'-lm'") and "-rdynamic" not in link[0],
          "\n".join(link) or out)
    code, out = rune(["build", "--target", "x"], project, env)
    check("an unchanged raw runtime is not built again",
          code == 0 and "Preparing" not in out, out)
    with open(manifest) as f:
        text = f.read()
    with open(manifest, "w") as f:
        f.write(text.replace('"-DFROM_THE_TABLE"', '"-DFROM_THE_TABLE", "-g"'))
    code, out = rune(["run", "--target", "x"], project, env)
    check("a raw runtime is built again when its flags change",
          code == 0 and "Preparing runtime" in out and "Hello from rawpkg!" in out, out)


def find_sdk():
    for p in [os.environ.get("WASI_SDK_PATH", ""), "/opt/wasi-sdk"]:
        if p and os.path.isfile(os.path.join(p, "bin", "clang")):
            return p
    return None


def webassembly(tmp, env):
    sdk = find_sdk()
    if not sdk or not shutil.which("wasmtime"):
        print("skip wasm: needs the WASI SDK and wasmtime")
        return
    env = dict(env, WASI_SDK_PATH=sdk)

    rune(["new", "hello"], tmp, env)
    hello = os.path.join(tmp, "hello")
    for target, triple in [("wasm", "wasm32-wasip1"),
                           ("wasm-threads", "wasm32-wasip1-threads")]:
        code, out = rune(["run", "--target", target], hello, env)
        check(f"{target}: a new package runs",
              code == 0 and "Hello from hello!" in out, out)
        module = os.path.join(hello, "target", target, "debug", "hello.wasm")
        check(f"{target}: the module is target/{target}/debug/hello.wasm",
              os.path.isfile(module))
        with open(module, "rb") as f:
            check(f"{target}: it is WebAssembly", f.read(4) == b"\0asm")

    code, out = rune(["build", "--target", "wasi"], hello, env)
    check("an alias builds into the foreign target's own directory",
          code == 0 and not os.path.exists(os.path.join(hello, "target", "wasi")),
          out)

    # The WebAssembly example, as its README runs it: arguments and a file
    # through WASI, and nothing outside the directory it was granted.
    example = os.path.join(tmp, "wasm-wordfreq")
    shutil.copytree(os.path.join(ROOT, "examples", "wasm-wordfreq"), example,
                    ignore=shutil.ignore_patterns("target"))
    code, out = rune(["run", "--", "sample.txt", "3"], example, env)
    check("examples/wasm-wordfreq runs under WASI",
          code == 0 and "sample.txt: 60 words, 20 different" in out and
          "10\tit" in out, out)
    module = os.path.join(example, "target", "wasm", "debug", "wordfreq.wasm")
    r = subprocess.run(["wasmtime", "run", "--dir=.", module, "/etc/hostname"],
                       cwd=example, capture_output=True, text=True, timeout=60)
    check("it cannot read outside the directory it was granted",
          r.returncode == 1 and "cannot read /etc/hostname" in r.stderr,
          r.stdout + r.stderr)

    # C and C++ halves cross along with the Rune.
    code, out = rune(["test", "-C", os.path.join(ROOT, "examples/project/ffi"),
                      "--target", "wasm"], tmp, env)
    check("wasm: the FFI example's tests pass", code == 0, out)

    # A few cases from the end-to-end suite, straight through runec, as the
    # foreign targets would build them. The threaded flavour is the one where
    # tasks can wait.
    cases = {
        "wasm32-wasip1": ["01_values", "04_classes", "06_closures",
                          "39_file_io", "68_streams"],
        "wasm32-wasip1-threads": ["70_thread_accounting", "92_async_basics",
                                  "92_async_tasks"],
    }
    for triple, names in cases.items():
        runtime = os.path.join(env["RUNE_HOME"], "runtime", triple)
        runner = ["wasmtime", "run", "--dir=."]
        if "threads" in triple:
            runner[2:2] = ["-W", "threads=y", "-S", "threads=y"]
        for name in names:
            case = os.path.join(ROOT, "tests", "cases", name + ".rune")
            module = os.path.join(tmp, f"{name}-{triple}.wasm")
            r = subprocess.run(
                [RUNEC, "--target", triple,
                 "--cc", os.path.join(sdk, "bin", triple + "-clang"),
                 "--sysroot", os.path.join(sdk, "share", "wasi-sysroot"),
                 "--runtime-dir", runtime, *case_flags(case), "-o", module,
                 case],
                capture_output=True, text=True, cwd=os.path.dirname(case))
            if r.returncode != 0:
                check(f"{triple}: {name} compiles", False, r.stderr[-2000:])
                continue
            r = subprocess.run([*runner, module], capture_output=True,
                               text=True, cwd=os.path.dirname(case), timeout=120)
            check(f"{triple}: {name}",
                  r.returncode == 0 and r.stdout.splitlines() == expected_lines(case),
                  r.stdout[-2000:] + r.stderr[-2000:])


def main():
    tmp = tempfile.mkdtemp(prefix="rune-targets-")
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"))
    try:
        resolution(tmp, env)
        raw_target(tmp, env)
        webassembly(tmp, env)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("\n" + "\n\n".join(failures))
        sys.exit(1)


if __name__ == "__main__":
    main()
