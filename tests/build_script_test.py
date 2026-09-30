#!/usr/bin/env python3
"""A package's build.rune, end to end, on this machine.

A package with a build script is built and run through `rune`, and each
thing a script can do is checked: its prepare phase sets `@Config` flags the
package's sources see; its finish phase runs once per executable, after the
link, and can name a different file for `rune run` to start; a warning it
gives is shown; a failure it reports stops the build with its message; and a
second build with nothing changed compiles nothing.

Invoked by CTest as `rune_build_script`; run it by hand with

    python3 tests/build_script_test.py build/bin
"""
import os, shutil, subprocess, sys, tempfile

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/bin")
failures = []


def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))


MANIFEST = """[package]
name = "scripted"
version = "0.1.0"
"""

MAIN = """import std::io

@Config(generated)
fn origin() -> String { "set by build.rune" }
@Config(!generated)
fn origin() -> String { "not set" }

@Config(mode == "fast")
fn mode() -> String { "fast" }
@Config(!(mode == "fast"))
fn mode() -> String { "other" }

fn main() -> i64 {
    io::println("config: " + origin() + ", mode " + mode())
    0
}
"""

SCRIPT = """import std::build
import std::io
import std::process

fn main() -> i64 {
    if build::preparing() {
        build::cfg("generated")
        build::cfgValue("mode", "fast")
        build::warning("prepared " + build::packageName() + " for " + build::target())
    }
    if build::finishing() {
        // What `rune run` starts instead: a wrapper that says so, then runs
        // the executable that was just linked.
        let wrapper = build::outDir() + "/" + build::artifactName() + "-wrapped.sh"
        let text = "#!/bin/sh\\necho wrapped by build.rune\\nexec '" + build::artifact() + "' \\"$@\\"\\n"
        if io::writeString(&wrapper, text) is Err(e) {
            build::fail("cannot write the wrapper")
        }
        build::run("chmod", ["+x", wrapper.$clone()])
        build::runWith(wrapper)
    }
    0
}
"""


LINK_SCRIPT = """import std::build
import std::io

fn main() -> i64 {
    if build::preparing() {
        build::cfg("generated")
        build::cfgValue("mode", "linked")
    }
    if build::linking() {
        let note = build::outDir() + "/linked-by-script"
        if io::writeString(&note, build::linkOutput()) is Err(e) {
            build::fail("cannot write the note")
        }
        var args = build::linkArguments()
        build::runAll("cc", &args)
    }
    0
}
"""


def rune(project, *args):
    env = dict(os.environ, RUNE_HOME=os.path.join(project, ".home"))
    r = subprocess.run([os.path.join(BIN, "rune"), *args, "--no-color"],
                       cwd=project, env=env, capture_output=True, text=True,
                       timeout=300)
    return r.returncode, r.stdout + r.stderr


def main():
    tmp = tempfile.mkdtemp(prefix="rune-script-")
    try:
        project = os.path.join(tmp, "scripted")
        os.makedirs(os.path.join(project, "src"))
        with open(os.path.join(project, "Rune.toml"), "w") as f:
            f.write(MANIFEST)
        with open(os.path.join(project, "src", "main.rune"), "w") as f:
            f.write(MAIN)
        with open(os.path.join(project, "build.rune"), "w") as f:
            f.write(SCRIPT)

        rc, out = rune(project, "run")
        check("a package with a build script builds and runs", rc == 0, out)
        check("the build script is compiled for this machine",
              "(build script)" in out, out)
        check("its prepare phase sets @Config flags, with and without a value",
              "config: set by build.rune, mode fast" in out, out)
        check("a warning it gives is shown",
              "warning: scripted: prepared scripted for host" in out, out)
        check("its finish phase names what `rune run` starts",
              "wrapped by build.rune\nconfig:" in out, out)

        rc, out = rune(project, "build")
        check("a second build with nothing changed compiles nothing",
              rc == 0 and "Compiling" not in out, out)

        with open(os.path.join(project, "build.rune"), "a") as f:
            f.write("\n// edited\n")
        rc, out = rune(project, "build")
        check("an edited build script is compiled again, and nothing else",
              rc == 0 and "(build script)" in out and
              "Compiling scripted v0.1.0\n" not in out, out)

        # The script as the linker: called with a linker's arguments, it
        # links with whatever it likes and leaves a note that it did.
        machine = subprocess.run(["cc", "-dumpmachine"], capture_output=True,
                                 text=True).stdout.strip()
        with open(os.path.join(project, "Rune.toml"), "a") as f:
            f.write('\n[target.own-link]\ntriple = "%s"\ncc = "cc"\nlinker = "build-script"\n'
                    'linker-kind = "driver"\nrunner = "env"\n' % machine)
        with open(os.path.join(project, "build.rune"), "w") as f:
            f.write(LINK_SCRIPT)
        rc, out = rune(project, "run", "--target", "own-link")
        check("with linker = \"build-script\", the script links the program",
              rc == 0 and "config: set by build.rune, mode other" in out and
              os.path.exists(os.path.join(project, "target", "own-link",
                                          "debug", "linked-by-script")), out)

        with open(os.path.join(project, "build.rune"), "w") as f:
            f.write("import std::build\nfn main() -> i64 {\n"
                    "    if build::preparing() { build::fail(\"no widgets\") }\n"
                    "    0\n}\n")
        rc, out = rune(project, "build")
        check("a failure it reports stops the build, saying why",
              rc != 0 and "failed in its prepare phase" in out and
              "no widgets" in out, out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        print("\n" + "\n\n".join(failures))
        sys.exit(1)


if __name__ == "__main__":
    main()
