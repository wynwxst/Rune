#!/usr/bin/env python3
"""Bare metal, driven through the toolchain itself.

Two halves, each skipped with a line saying so where the machine cannot run
it:

* Freestanding programs on this machine. Every case in tests/freestanding/
  is compiled `@runtime(none)` for Linux x86_64 — no C library, no hosted
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
import os, platform, shutil, subprocess, sys, tempfile

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
    "heap":     ("sys",  True,  0,   "freed the pair\nfreed a leaf\nfreed a leaf\nlive after scope 0"),
    "optional": ("sys",  True,  23,  ""),
    "noalloc":  ("mini", True,  101, "panic: this program allocates, and declares no @allocator"),
    "moved":    ("sys",  False, None, "'b' has been moved out of [E0273]"),
    "hosted":   (None,   False, None, "'greet' needs the hosted runtime, and this program is built without one [E0542]"),
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

    kernel = os.path.join(project, "target", "bare-x86", "debug", "tetris")
    serial = os.path.join(tmp, "tetris-serial.txt")
    monitor = os.path.join(tmp, "tetris-monitor.sock")
    qemu = subprocess.Popen(
        ["sh", "tools/run.sh", kernel, "-display", "none", "-no-reboot",
         "-serial", "file:" + serial,
         "-monitor", "unix:" + monitor + ",server,nowait"],
        cwd=project, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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

        for _ in range(100):
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

        w, h, pixels = screen("menu")
        red = sum(1 for i in range(0, len(pixels), 3)
                  if pixels[i] > 200 and pixels[i + 1] < 40 and pixels[i + 2] < 40)
        lit = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] != b"\0\0\0")
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
        qemu.kill()


def main():
    tmp = tempfile.mkdtemp(prefix="rune-bare-")
    try:
        freestanding(tmp)
        toyos(tmp)
        tetris(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("\n" + "\n\n".join(failures))
        sys.exit(1)


if __name__ == "__main__":
    main()
