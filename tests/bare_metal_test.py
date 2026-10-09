#!/usr/bin/env python3
"""Bare metal, driven through the toolchain itself.

Two halves, each skipped with a line saying so where the machine cannot run
it:

* Freestanding programs on this machine. Every case in tests/freestanding/
  is compiled `#runtime(none)` for Linux x86_64 — no C library, no hosted
  runtime, two system calls by inline assembly — with `--safety full`, and
  run. Each failed check must reach the program's own panic handler with the
  right message; the heap must go through its own allocator; the Zombie
  borrow checker must still refuse a use after a move; a use of the hosted
  runtime must be refused at compile time.

* examples/toyos, booted. `rune run` builds the kernel for `bare-x86` and
  boots it under qemu-system-i386: every check passes and the machine is
  switched off (QEMU exits 0). Booted with `-append panic`, it trips a bounds
  check on purpose, and the kernel's panic handler ends the run through
  QEMU's debug-exit device (exit status 3).

Invoked by CTest as `rune_bare_metal`; run it by hand with

    python3 tests/bare_metal_test.py build/bin
"""
import os, platform, re, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/bin")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CASES = os.path.join(ROOT, "tests", "freestanding")

failures = []


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))


# name: (companion, compiles, exit status, text the output must contain)
PROGRAMS = {
    "bounds":   ("sys",  True,  101, "panic: index 9 is out of bounds for a collection of length 4"),
    "overflow": ("sys",  True,  101, "panic: integer overflow in `+`"),
    "divzero":  ("sys",  True,  101, "panic: division by zero"),
    "unwrap":   ("sys",  True,  101, "panic: unwrapped an empty Option"),
    "stdpanic": ("sys",  True,  101, "panic: out of fuel"),
    "heap":     ("sys",  True,  0,   "freed the pair\nfreed a leaf\nfreed a leaf\nlive after scope 0"),
    "optional": ("sys",  True,  23,  ""),
    "noalloc":  ("mini", True,  101, "panic: this program allocates, and declares no #allocator"),
    "moved":    ("sys",  False, None, "'b' has been moved out of [E0273]"),
    "hosted":   (None,   False, None, "'greet' needs the hosted runtime, and this program is built without one [E0542]"),
    "literals": ("sys",  True,  0,   "no annotation needed\na literal is a CString here\nin an array\n"),
    "printing": ("sys", True,  0,    "hello from freestanding: 42 0.30000000000000004\n0.6667 1e+301\n-1.25\n"),
}


def freestanding(tmp):
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "AMD64"):
        print("skip freestanding: the cases make Linux x86_64 system calls")
        return
    for name, (companion, compiles, status, text) in PROGRAMS.items():
        exe = os.path.join(tmp, name)
        inputs = [os.path.join(CASES, name + ".rune")]
        if companion:
            inputs.append(os.path.join(CASES, companion + ".rune"))
        r = subprocess.run([os.path.join(BIN, "runec"), "--safety", "full",
                            "--module", "fs", "--no-color", "-o", exe, *inputs],
                           capture_output=True, text=True)
        if not compiles:
            check(f"{name}: refused at compile time",
                  r.returncode != 0 and text in r.stderr, r.stderr[-3000:])
            continue
        if r.returncode != 0:
            check(f"{name}: compiles", False, r.stderr[-3000:])
            continue
        undefined = subprocess.run(["nm", "-u", exe], capture_output=True,
                                   text=True).stdout.strip()
        check(f"{name}: links against nothing", undefined == "", undefined)
        run = subprocess.run([exe], capture_output=True, text=True, timeout=30)
        check(f"{name}: exits {status}{' saying so' if text else ''}",
              run.returncode == status and text in run.stdout,
              f"exit {run.returncode}\n{run.stdout}{run.stderr}")


def minimal(tmp):
    """`freestanding_type = "minimal"`: the runtime keeps only what a program
    cannot run without, and says so when a program needs more. Objects only,
    so it runs on every host."""
    runec = os.path.join(BIN, "runec")
    sizes = {}
    for kind in ("full", "minimal"):
        obj = os.path.join(tmp, f"minimal_{kind}.o")
        r = subprocess.run([runec, "--no-color", "-O2", "-c", "--cfg",
                            f"freestanding_type={kind}", "-o", obj,
                            os.path.join(CASES, "minimal.rune")],
                           capture_output=True, text=True)
        check(f"minimal.rune compiles with freestanding_type={kind}",
              r.returncode == 0, r.stderr[-3000:])
        if r.returncode != 0:
            return
        sizes[kind] = os.path.getsize(obj)
        symbols = subprocess.run(["nm", obj], capture_output=True,
                                 text=True).stdout
        if kind == "minimal":
            check("the minimal runtime leaves out floats as text and hashing",
                  "rune_string_from_f64" not in symbols and
                  "rune_hash_mix" not in symbols, symbols[-2000:])
        else:
            check("the full runtime has floats as text",
                  "rune_string_from_f64" in symbols, symbols[-2000:])
    check("the minimal runtime makes a smaller program",
          sizes["minimal"] < sizes["full"], str(sizes))
    r = subprocess.run([runec, "--no-color", "-c", "--cfg",
                        "freestanding_type=minimal", "-o",
                        os.path.join(tmp, "minimal_float.o"),
                        os.path.join(CASES, "minimal_float.rune")],
                       capture_output=True, text=True)
    check("a float as text on the minimal runtime is refused, saying why",
          r.returncode != 0 and
          "needs more of the runtime than `freestanding_type = \"minimal\"` keeps"
          in r.stderr and "floats as text" in r.stderr, r.stderr[-3000:])
    r = subprocess.run([runec, "--no-color", "-c", "--cfg",
                        "freestanding_type=tiny", "-o",
                        os.path.join(tmp, "tiny.o"),
                        os.path.join(CASES, "minimal.rune")],
                       capture_output=True, text=True)
    check("an unknown freestanding_type is refused",
          r.returncode != 0 and "E0545" in r.stderr, r.stderr[-3000:])


def tiers():
    """The whole standard library, built as a `#runtime(none)` program would
    see it: every module has to compile that way, and `runec --tiers` says
    which functions work on bare metal. The answers the documentation's
    badges show are checked against what is known."""
    r = subprocess.run([os.path.join(BIN, "runec"), "--tiers", "--no-color"],
                       capture_output=True, text=True)
    check("every module of the standard library builds freestanding",
          r.returncode == 0 and r.stderr.strip() == "", r.stderr[-3000:])
    tier = {}
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2:
            tier[parts[1]] = parts[0]
    stdlib = os.path.join(ROOT, "stdlib", "std")
    modules = set()
    for base, _, files in os.walk(stdlib):
        for f in files:
            # A module of marks, intrinsics and macros has nothing to place.
            if f.endswith(".rune") and re.search(
                    r"^pub fn [^\n]*\{", open(os.path.join(base, f)).read(), re.M):
                rel = os.path.relpath(os.path.join(base, f), stdlib)[:-5]
                modules.add("std::" + rel.replace(os.sep, "::"))
    reported = set()
    for name in tier:
        parts = name.split("::")
        for cut in range(1, len(parts)):
            reported.add("::".join(parts[:cut]))
    missing = sorted(m for m in modules if m not in reported)
    check("every module has its functions placed", not missing, ", ".join(missing))
    expect = {
        "std::collections::vector::Vector::push": "bare",
        "std::dictionary::Map::put": "bare",
        "std::io::println": "bare",
        # Floats as text are in the full freestanding runtime only.
        "std::fmt::fixed": "full",
        "std::any::Any::get": "full",
        "std::process::panic": "bare",
        "std::json::write": "full",      # numbers are written as floats
        "std::random::seeded": "bare",
        "std::io::readLine": "hosted",
        "std::thread::spawn": "hosted",
        "std::process::exit": "hosted",
        "std::random::new": "hosted",
        # A generic is bare when any instantiation is: `Option<TcpStream>`
        # closing a socket is the socket's need, not `unwrap`'s.
        "std::option::Option::unwrap": "bare",
        "std::option::Option::expect": "bare",
        "std::result::Result::unwrap": "bare",
        # Templates nothing in the library instantiates are placed too.
        "std::option::Option::map": "bare",
        "std::dictionary::Set::add": "bare",
        "std::thread::Mutex::get": "hosted",
        # Intrinsics, by what the compiler writes for them.
        "std::mem::size_of": "bare",
        "std::reflect::describe": "bare",
        "std::any::Any::holds": "full",  # `Any` is not in the minimal runtime
        "std::any::Any::typeName": "hosted",
        "std::cxx::alloc": "hosted",
    }
    wrong = [f"{k}: {tier.get(k, 'missing')}, not {v}" for k, v in expect.items()
             if tier.get(k) != v]
    check("bare metal and hosted where they are known to be", not wrong, "\n".join(wrong))
    kinds = set(tier.values())
    check("--tiers answers bare, full or hosted", kinds == {"bare", "full", "hosted"},
          str(kinds))
    # A program that chose its runtime gets that runtime's answer alone.
    r = subprocess.run([os.path.join(BIN, "runec"), "--tiers", "--no-color",
                        "--cfg", "freestanding_type=minimal"],
                       capture_output=True, text=True)
    minimal = {l.split()[1]: l.split()[0] for l in r.stdout.splitlines() if len(l.split()) >= 2}
    check("--tiers for the minimal runtime: floats as text need more",
          minimal.get("std::fmt::fixed") == "hosted" and minimal.get("std::io::println") == "bare",
          str((minimal.get("std::fmt::fixed"), minimal.get("std::io::println"))))


INTRINSICS = """#runtime(none)
#entry(none)
import std::mem
import std::reflect
import std::arch
import std::asm
import std::any

struct P { pub a: i64 = 0 }

#export("probe")
#safe("a probe of every intrinsic said to work on bare metal")
fn probe() -> i64 {
    var x: i64 = 3
    var n = mem::size_of<i64>() as i64 + mem::align_of<i64>() as i64
    if mem::is_counted<i64>() { n += 1 }
    n += mem::hash(&x) as i64
    if mem::equals(&x, &x) { n += 1 }
    n += mem::zeroed<i64>()
    n += unsafe { mem::take(&var x as *var i64) }
    unsafe { mem::store(&var x as *var i64, 4) }
    n += mem::replace(&var x, 5)
    unsafe { mem::drop_at<i64>(&var x as *var i64) }
    mem::retain(1)
    mem::release(1)
    n += reflect::typeName<P>().$length() + reflect::typeId<P>() as i64
    n += reflect::kindOf<P>() as i64 + reflect::sizeOf<P>() as i64
    n += reflect::alignOf<P>() as i64 + reflect::strideOf<P>() as i64
    n += reflect::fieldCount<P>() as i64 + reflect::fieldName<P>(0).$length()
    if reflect::isSend<P>() && reflect::isSync<P>() { n += 1 }
    n += reflect::describe(P { a: 1 }).$length()
    n += arch::triple().$length()
    unsafe { asm::run("nop", "") }
    let a: Any = 5
    if a.holds::<i64>() { n += a.get::<i64>() ?? 0 }
    n + a.expect::<i64>()
}
"""


def intrinsics(tmp):
    """Every intrinsic `--tiers` says is bare compiles in a `#runtime(none)`
    program, and one it says is hosted is refused there. An intrinsic has no
    body for `--tiers` to follow, so its answer comes from a table in the
    code generator; this is what keeps the table honest."""
    runec = os.path.join(BIN, "runec")
    src = os.path.join(tmp, "intrinsics.rune")
    open(src, "w").write(INTRINSICS)
    r = subprocess.run([runec, "-c", src, "-o", src + ".o"], capture_output=True, text=True)
    check("every bare intrinsic builds freestanding", r.returncode == 0,
          r.stdout + r.stderr)
    hosted = os.path.join(tmp, "typename.rune")
    open(hosted, "w").write(INTRINSICS.split("#export")[0].replace("import std::reflect\n", "") +
                            '#export("probe")\n#safe("a probe")\nfn probe() -> i64 {\n'
                            '    let a: Any = 5\n    a.typeName().$length()\n}\n')
    r = subprocess.run([runec, "-c", hosted, "-o", hosted + ".o"], capture_output=True, text=True)
    check("Any::typeName is refused freestanding, as --tiers says",
          r.returncode != 0 and "E0542" in r.stdout + r.stderr, r.stdout + r.stderr)


def toyos(tmp):
    needed = ["qemu-system-i386", "clang", "ld.lld"]
    missing = [t for t in needed if not shutil.which(t)]
    if missing:
        print("skip toyos: needs " + ", ".join(missing))
        return
    project = os.path.join(tmp, "toyos")
    shutil.copytree(os.path.join(ROOT, "examples", "toyos"), project,
                    ignore=shutil.ignore_patterns("target"))
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"))
    rune = os.path.join(BIN, "rune")

    r = subprocess.run([rune, "run", "--no-color"], cwd=project, env=env,
                       capture_output=True, text=True, timeout=300)
    out = r.stdout + r.stderr
    check("toyos boots, passes every check and switches the machine off",
          r.returncode == 0 and "all checks passed" in out and "[FAIL]" not in out,
          f"exit {r.returncode}\n{out[-4000:]}")
    check("toyos reaps every process back to the heap",
          "reaped pid 1 (init)" in out and "every process reaped, every byte back" in out,
          out[-4000:])

    r = subprocess.run([rune, "run", "--no-color", "--", "-append", "panic"],
                       cwd=project, env=env, capture_output=True, text=True,
                       timeout=300)
    out = r.stdout + r.stderr
    check("toyos: a failed bounds check reaches the kernel's panic handler",
          r.returncode == 3 and
          "KERNEL PANIC: index 4 is out of bounds for a collection of length 4" in out,
          f"exit {r.returncode}\n{out[-4000:]}")

    r = subprocess.run([rune, "run", "--release", "--no-color"], cwd=project,
                       env=env, capture_output=True, text=True, timeout=300)
    check("toyos, optimised, passes too",
          r.returncode == 0 and "all checks passed" in r.stdout,
          f"exit {r.returncode}\n{(r.stdout + r.stderr)[-4000:]}")


WRAPPED_GCC = """#!/bin/sh
# Stands in for a GNU i686-elf cross compiler: refuses clang-only flags.
for a in "$@"; do
  case "$a" in --target=*|-fuse-ld=*|-Wno-unused-command-line-argument)
    echo "i686-elf-gcc: error: unrecognized option '$a'" >&2; exit 1;;
  esac
done
exec gcc -m32 "$@"
"""

WRAPPED_LD = """#!/bin/sh
# Stands in for a GNU i686-elf linker: refuses anything meant for a driver.
for a in "$@"; do
  case "$a" in -Wl,*|--target=*|-fuse-ld=*|-nostdlib|-static|-f*)
    echo "i686-elf-ld: unrecognized option '$a'" >&2; exit 1;;
  esac
done
exec ld -m elf_i386 "$@"
"""


def gnu_toolchain(tmp):
    """toyos, built with a GNU cross toolchain rather than clang and lld.

    `i686-elf-gcc` and `i686-elf-ld` are stood in for by the host's gcc and
    GNU ld, behind scripts that refuse any flag a GNU tool would: nothing
    the build adds of its own may assume clang."""
    needed = ["qemu-system-i386", "gcc", "ld"]
    missing = [t for t in needed if not shutil.which(t)]
    if missing:
        print("skip GNU toolchain: needs " + ", ".join(missing))
        return
    probe = subprocess.run(["ld", "-m", "elf_i386", "-V"], capture_output=True, text=True)
    if probe.returncode != 0:
        print("skip GNU toolchain: this ld cannot link 32-bit x86")
        return
    tools = os.path.join(tmp, "gnu-bin")
    os.makedirs(tools)
    for name, text in (("i686-elf-gcc", WRAPPED_GCC), ("i686-elf-ld", WRAPPED_LD)):
        path = os.path.join(tools, name)
        with open(path, "w") as f:
            f.write(text)
        os.chmod(path, 0o755)
    project = os.path.join(tmp, "toyos-gnu")
    shutil.copytree(os.path.join(ROOT, "examples", "toyos"), project,
                    ignore=shutil.ignore_patterns("target"))
    # The example's own `[target.i686-elf]`, as a macOS user would run it.
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"),
               PATH=tools + os.pathsep + os.environ["PATH"])
    rune = os.path.join(BIN, "rune")
    r = subprocess.run([rune, "targets", "--no-color"], cwd=project, env=env,
                       capture_output=True, text=True, timeout=60)
    listing = r.stdout[r.stdout.find("  i686-elf "):]
    check("rune targets shows the GNU tools and no clang flags",
          "links with i686-elf-ld (ld)" in listing and "--target=" not in
          listing.split("\n  ")[0], r.stdout)
    r = subprocess.run([rune, "run", "--target", "i686-elf", "--no-color", "-v"],
                       cwd=project, env=env, capture_output=True, text=True,
                       timeout=300)
    out = r.stdout + r.stderr
    check("toyos builds with i686-elf-gcc and i686-elf-ld, and boots",
          r.returncode == 0 and "all checks passed" in out,
          f"exit {r.returncode}\n{out[-4000:]}")
    link = [l for l in out.splitlines() if "link: " in l]
    check("the link is the objects, -o and the linker script — nothing more",
          link and "'-T'" in link[0] and "-Wl," not in link[0] and
          "-nostdlib" not in link[0], "\n".join(link) or out[-2000:])


def read_ppm(path):
    """A QEMU screendump: width, height and RGB bytes."""
    with open(path, "rb") as f:
        data = f.read()
    magic, size, _depth, pixels = data.split(b"\n", 3)
    w, h = map(int, size.split())
    return w, h, pixels


def tetris(tmp):
    needed = ["qemu-system-i386", "clang", "ld.lld"]
    missing = [t for t in needed if not shutil.which(t)]
    if not shutil.which("llvm-objcopy") and not shutil.which("objcopy"):
        missing.append("llvm-objcopy")
    if missing:
        print("skip tetris-os: needs " + ", ".join(missing))
        return
    import socket
    import time
    project = os.path.join(tmp, "tetris-os")
    shutil.copytree(os.path.join(ROOT, "examples", "tetris-os"), project,
                    ignore=shutil.ignore_patterns("target"))
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"))
    r = subprocess.run([os.path.join(BIN, "rune"), "build", "--no-color"],
                       cwd=project, env=env, capture_output=True, text=True,
                       timeout=600)
    check("tetris-os builds", r.returncode == 0, r.stdout + r.stderr)
    if r.returncode != 0:
        return

    image = os.path.join(project, "target", "bare-x86", "debug", "tetris.img")
    check("its build script lays the kernel out as a disk image",
          os.path.exists(image) and os.path.getsize(image) > 512 and
          open(image, "rb").read()[510:512] == b"\x55\xaa",
          r.stdout + r.stderr)
    serial = os.path.join(tmp, "tetris-serial.txt")
    monitor = os.path.join(tmp, "tetris-monitor.sock")
    # `rune run` from inside the package: it finds the manifest above, and
    # boots the image the build script made through the target's runner.
    qemu = subprocess.Popen(
        [os.path.join(BIN, "rune"), "run", "--no-color", "--",
         "-display", "none", "-no-reboot",
         "-serial", "file:" + serial,
         "-monitor", "unix:" + monitor + ",server,nowait"],
        cwd=os.path.join(project, "src"), env=env, start_new_session=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        def log():
            try:
                with open(serial) as f:
                    return f.read()
            except OSError:
                return ""

        def wait_for(text, seconds):
            deadline = time.time() + seconds
            while time.time() < deadline:
                if text in log():
                    return True
                time.sleep(0.2)
            return False

        for _ in range(300):
            if os.path.exists(monitor):
                break
            time.sleep(0.1)
        mon = socket.socket(socket.AF_UNIX)
        mon.connect(monitor)
        mon.settimeout(1)

        def command(line):
            mon.sendall((line + "\n").encode())
            time.sleep(0.25)
            try:
                mon.recv(65536)
            except OSError:
                pass

        def screen(name):
            path = os.path.join(tmp, name + ".ppm")
            command("screendump " + path)
            time.sleep(0.5)
            return read_ppm(path)

        check("tetris-os boots from its own boot sector into Rune",
              wait_for("tetris: booted into Rune", 30), log())
        check("tetris-os finds the SoundBlaster 16",
              wait_for("sb16: dsp version 4", 30), log())
        check("tetris-os builds the four parts of the theme",
              wait_for("music parts of 62 195 128 58 notes", 30), log())
        check("tetris-os reaches its menu", wait_for("tetris: menu", 30), log())

        # The log line comes as the menu starts; the first frame reaches the
        # screen a moment later, so give it a few tries.
        for _ in range(10):
            w, h, pixels = screen("menu")
            red = sum(1 for i in range(0, len(pixels), 3)
                      if pixels[i] > 200 and pixels[i + 1] < 40 and pixels[i + 2] < 40)
            lit = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] != b"\0\0\0")
            if lit > w * h // 20:
                break
        check("the menu is drawn, and is not the panic screen",
              lit > w * h // 20 and red < w * h // 2, f"{lit} lit, {red} red")

        # Held, as a person holds a key: the game reads the keyboard once a
        # frame, and `sendkey`'s own press is shorter than one.
        command("sendkey ret 300")
        check("Enter starts a game", wait_for("tetris: new game", 10), log())

        for _ in range(80):
            if "game over" in log():
                break
            command("sendkey spc")
        check("hard drops fill the board and end the game",
              wait_for("tetris: game over, score", 20), log())

        time.sleep(0.5)
        w, h, pixels = screen("gameover")
        grey = sum(1 for i in range(0, len(pixels), 3)
                   if pixels[i:i + 3] == b"\x80\x80\x80")
        check("the GAME OVER box is on the screen", grey > 1000, f"{grey} grey pixels")
        check("nothing panicked", "panic" not in log(), log())
        command("quit")
    finally:
        # `rune run` and the QEMU it started, together.
        try:
            os.killpg(qemu.pid, 9)
        except OSError:
            pass
        qemu.wait()


def main():
    tmp = tempfile.mkdtemp(prefix="rune-bare-")
    try:
        freestanding(tmp)
        minimal(tmp)
        tiers()
        intrinsics(tmp)
        toyos(tmp)
        gnu_toolchain(tmp)
        tetris(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("\n" + "\n\n".join(failures))
        sys.exit(1)


if __name__ == "__main__":
    main()
