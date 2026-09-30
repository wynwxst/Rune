#!/usr/bin/env python3
"""The example ecosystem: build its registries, serve them, or prove it works.

    python3 examples/package/ecosystem.py build          # packages/ -> registry/, lab/ -> registry-lab/
    python3 examples/package/ecosystem.py serve [port]   # rune pkg server --serve, both registries
    python3 examples/package/ecosystem.py check          # the whole cycle, isolated

There are two registries. `ecosystem` is the main one, built from
`packages/`; `lab` is built from `lab/` and carries the lab's own build of
`logger`, which `apps/dashboard` takes from there by name. Two is what it
takes to show what a registry's name is for.

`check` uses a temporary RUNE_HOME, so it touches neither the real cache nor
the real install store: it builds both registries, adds them to the client,
runs every package's tests against dependencies installed from the
registries, builds and runs both apps, and shows what `rune deps` and
`rune installed` say. Pass `--rune <path>` to use a particular `rune`; the
default is the one on PATH, or build-release/bin/rune beside this tree.
"""
import os, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
APPS = os.path.join(HERE, "apps")

# (name the registry declares, packages it is built from, where it is built)
REGISTRIES = [
    ("ecosystem", os.path.join(HERE, "packages"), os.path.join(HERE, "registry")),
    ("lab", os.path.join(HERE, "lab"), os.path.join(HERE, "registry-lab")),
]

def find_rune(argv):
    if "--rune" in argv:
        return argv[argv.index("--rune") + 1]
    for candidate in (shutil.which("rune"),
                      os.path.join(ROOT, "build-release", "bin", "rune"),
                      os.path.join(ROOT, "build", "bin", "rune")):
        if candidate and os.path.exists(candidate):
            return candidate
    sys.exit("rune not found; build the toolchain or pass --rune <path>")

def releases(packages):
    """Every <packages>/<name>/<version>/, oldest version first so the index
    reads in order."""
    out = []
    for name in sorted(os.listdir(packages)):
        pdir = os.path.join(packages, name)
        if not os.path.isdir(pdir):
            continue
        versions = [v for v in os.listdir(pdir) if os.path.isdir(os.path.join(pdir, v))]
        versions.sort(key=lambda v: [int(p) for p in v.split(".")])
        for v in versions:
            out.append((name, v, os.path.join(pdir, v)))
    return out

def run(rune, args, cwd, env, check=True):
    r = subprocess.run([rune, *args, "--no-color"], cwd=cwd, env=env,
                       capture_output=True, text=True)
    if check and r.returncode != 0:
        print(r.stdout + r.stderr)
        sys.exit(f"rune {' '.join(args)} failed in {cwd}")
    return r.stdout + r.stderr

def build_one(rune, env, name, packages, into):
    shutil.rmtree(into, ignore_errors=True)
    run(rune, ["registry", "init", into, "--name", name], HERE, env)
    for pkg, version, path in releases(packages):
        out = run(rune, ["registry", "--addPackage", path, "--dir", into], HERE, env)
        print(out.strip().split("\n")[-1])
    print(f"registry '{name}' written to {into}")

def build(rune, env, where=None):
    """Both registries; under `where` instead of beside this script when given."""
    for name, packages, into in REGISTRIES:
        if where:
            into = os.path.join(where, os.path.basename(into))
        build_one(rune, env, name, packages, into)

def serve(rune, env, port):
    """The main registry on `port`, the lab on the next one."""
    for name, packages, into in REGISTRIES:
        if not os.path.exists(os.path.join(into, "index.toml")):
            build_one(rune, env, name, packages, into)
    main_name, _, main_dir = REGISTRIES[0]
    lab_name, _, lab_dir = REGISTRIES[1]
    lab = subprocess.Popen([rune, "registry", "--serve", "--dir", lab_dir, "--port", str(port + 1)])
    print(f"'{lab_name}' on port {port + 1}: rune pkg server add http://localhost:{port + 1}")
    try:
        subprocess.run([rune, "registry", "--serve", "--dir", main_dir, "--port", str(port)])
    finally:
        lab.terminate()

def check(rune):
    tmp = tempfile.mkdtemp(prefix="rune-ecosystem-")
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"))
    failed = 0
    try:
        print("== building the registries")
        build(rune, env, tmp)
        print("\n== adding them to a fresh client")
        for name, _, into in REGISTRIES:
            url = "file://" + os.path.join(tmp, os.path.basename(into))
            print(run(rune, ["registry", "add", url], HERE, env).strip())
        print(run(rune, ["registry", "list"], HERE, env).strip())
        print(run(rune, ["search", "."], HERE, env).strip())
        print(run(rune, ["desc", "logger"], HERE, env).strip())

        print("\n== every package's tests, against dependencies from the registries")
        for name, packages, _ in REGISTRIES:
            work = os.path.join(tmp, "work-" + name)
            shutil.copytree(packages, work)
            for pkg, version, _ in releases(packages):
                path = os.path.join(work, pkg, version)
                r = subprocess.run([rune, "test", "--no-color"], cwd=path, env=env,
                                   capture_output=True, text=True)
                verdict = (r.stdout + r.stderr).strip().split("\n")[-1]
                print(f"  {name}::{pkg} {version}: {verdict}")
                if r.returncode != 0:
                    failed += 1
                    print(r.stdout + r.stderr)

        print("\n== the apps")
        apps = os.path.join(tmp, "apps")
        shutil.copytree(APPS, apps)
        for app in ("dashboard", "legacy"):
            path = os.path.join(apps, app)
            print(f"-- {app}: rune run")
            out = run(rune, ["run"], path, env)
            print("\n".join("   " + l for l in out.strip().split("\n")))
            if os.path.isdir(os.path.join(path, "tests")):
                r = subprocess.run([rune, "test", "--no-color"], cwd=path, env=env,
                                   capture_output=True, text=True)
                print("   " + (r.stdout + r.stderr).strip().split("\n")[-1])
                if r.returncode != 0:
                    failed += 1
            print(f"-- {app}: rune deps")
            print("\n".join("   " + l for l in run(rune, ["deps"], path, env).strip().split("\n")))

        print("\n== what is installed, and who uses it")
        print(run(rune, ["installed"], HERE, env).strip())
        print("-- from the lab alone")
        print(run(rune, ["installed", "--registry", "lab"], HERE, env).strip())
        # The main registry has a logger too; the dashboard's is the lab's.
        out = run(rune, ["desc", "ecosystem::logger"], HERE, env)
        if "0.9.0" not in out or "also in:      lab" not in out:
            failed += 1
            print("desc ecosystem::logger did not show 0.9.0 and the lab copy:\n" + out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failed:
        sys.exit(f"\n{failed} step(s) failed")
    print("\necosystem: every package tests clean and both apps run")

def main(argv):
    rune = find_rune(argv)
    argv = [a for a in argv if a != "--rune"]
    cmd = argv[1] if len(argv) > 1 and not argv[1].startswith("/") else "check"
    if cmd == "build":
        build(rune, dict(os.environ))
    elif cmd == "serve":
        port = int(argv[2]) if len(argv) > 2 else 7878
        serve(rune, dict(os.environ), port)
    elif cmd == "check":
        check(rune)
    else:
        sys.exit(__doc__)

if __name__ == "__main__":
    main(sys.argv)
