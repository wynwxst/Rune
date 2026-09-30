#!/usr/bin/env python3
"""End-to-end test of the package registry, driven through `rune` itself.

Runs with an isolated RUNE_HOME in a temporary directory, so nothing here
touches the real cache or install store. Invoked by CTest as
`rune_registry`; run it by hand with

    python3 tests/registry_test.py build-release/bin/rune
"""
import os, subprocess, sys, tempfile, shutil, re

RUNE = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build-release/bin/rune")

failures = []

def run(args, cwd, env, expect=0):
    r = subprocess.run([RUNE, *args, "--no-color"], cwd=cwd, env=env,
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    if expect is not None and r.returncode != expect:
        failures.append(f"rune {' '.join(args)} in {os.path.basename(cwd)}: exit {r.returncode}\n{out}")
    return out

def check(name, cond, detail=""):
    print(("ok   " if cond else "FAIL ") + name)
    if not cond:
        failures.append(name + ("\n" + detail if detail else ""))

def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)

def nameless_named(tmp, name):
    """A fresh registry directory whose index declares `name`, for checking
    that two registries cannot both be known by it."""
    d = os.path.join(tmp, "same-" + name)
    os.makedirs(d, exist_ok=True)
    write(os.path.join(d, "index.toml"), f'[registry]\nname = "{name}"\nformat = 1\n')
    return d

def set_version(project, version):
    p = os.path.join(project, "Rune.toml")
    s = open(p).read()
    s = re.sub(r'^version = "[^"]*"', f'version = "{version}"', s, count=1, flags=re.M)
    open(p, "w").write(s)

def main():
    tmp = tempfile.mkdtemp(prefix="rune-registry-")
    env = dict(os.environ, RUNE_HOME=os.path.join(tmp, "home"))
    try:
        # Two library packages: shapes depends on geometry 0.2.
        run(["new", "geometry", "--lib"], tmp, env)
        write(os.path.join(tmp, "geometry/src/lib.rune"),
              "pub struct Point { pub x: f64, pub y: f64 }\n"
              "pub fn distance(a: Point, b: Point) -> f64 {\n"
              "    let dx = a.x - b.x\n    let dy = a.y - b.y\n"
              "    std::math::squareRoot(dx * dx + dy * dy)\n}\n")
        run(["new", "shapes", "--lib"], tmp, env)
        write(os.path.join(tmp, "shapes/src/lib.rune"),
              "import geometry\n"
              "pub struct Circle { pub centre: geometry::Point, pub radius: f64 }\n"
              "pub fn contains(c: Circle, p: geometry::Point) -> bool {\n"
              "    geometry::distance(c.centre, p) <= c.radius\n}\n")
        p = os.path.join(tmp, "shapes/Rune.toml")
        s = open(p).read().replace("[dependencies]\n", '[dependencies]\ngeometry = "0.2"\n')
        set_version(os.path.join(tmp, "shapes"), "1.0.0")
        open(p, "w").write(s.replace('version = "0.1.0"', 'version = "1.0.0"'))

        # A registry with geometry 0.1.0, 0.2.0 and shapes 1.0.0.
        reg = os.path.join(tmp, "reg")
        run(["registry", "init", reg], tmp, env)
        run(["registry", "--addPackage", "geometry", "--dir", reg], tmp, env)
        set_version(os.path.join(tmp, "geometry"), "0.2.0")
        run(["registry", "--addPackage", "geometry", "--dir", reg], tmp, env)
        run(["registry", "--addPackage", "shapes", "--dir", reg], tmp, env)
        index = open(os.path.join(reg, "index.toml")).read()
        check("index lists three releases", index.count("[[release]]") == 3, index)
        check("index records shapes' dependency", 'dependencies = ["geometry 0.2"]' in index)
        check("duplicate release is refused",
              "already in this registry" in run(["registry", "--addPackage", "shapes", "--dir", reg], tmp, env, expect=1))

        # A client that uses it through a file:// URL. The registry calls
        # itself `reg` (its directory); here it is known as `local`.
        check("the index declares the registry's name", 'name = "reg"' in index, index)
        out = run(["registry", "add", "file://" + reg, "--name", "local"], tmp, env)
        check("registry added with a warning", "3 releases" in out and "not monitored" in out, out)
        check("the alias is explained", "calls itself 'reg'" in out, out)
        out = run(["registry", "list"], tmp, env)
        check("server list shows the alias, the url and the declared name",
              "local" in out and "file://" + reg in out and "calls itself 'reg'" in out, out)
        out = run(["registry", "add", "file://" + reg], tmp, env)
        check("adding the same url again is a no-op", "already configured as 'local'" in out, out)
        # A registry with no name is refused.
        nameless = os.path.join(tmp, "nameless")
        os.makedirs(nameless)
        write(os.path.join(nameless, "index.toml"), "[registry]\nformat = 1\n")
        out = run(["registry", "add", nameless], tmp, env, expect=1)
        check("a nameless registry is refused", "must provide a name" in out, out)
        out = run(["registry", "add", nameless, "--name", "bad name"], tmp, env, expect=2)
        check("a bad alias is refused", "cannot name a registry" in out, out)
        out = run(["search", "geo|shap"], tmp, env)
        check("search finds both by regex", "geometry" in out and "shapes" in out, out)
        out = run(["search", "zzz"], tmp, env)
        check("search reports nothing matching", "nothing matches" in out, out)
        out = run(["desc", "shapes"], tmp, env)
        check("desc shows dependencies and versions", "geometry 0.2" in out and "1.0.0" in out, out)

        # A project that adds shapes: geometry comes along, both are pinned.
        app = os.path.join(tmp, "app")
        run(["new", "app"], tmp, env)
        out = run(["add", "shapes"], app, env)
        check("add installs the transitive dependency", "Installed geometry v0.2.0" in out and "Installed shapes v1.0.0" in out, out)
        manifest = open(os.path.join(app, "Rune.toml")).read()
        check("Rune.toml gained the dependency", 'shapes = "1.0.0"' in manifest, manifest)
        lock = open(os.path.join(app, "Rune.lock")).read()
        check("Rune.lock pins both", 'name = "geometry"' in lock and 'name = "shapes"' in lock, lock)
        check("installed once, under RUNE_HOME",
              os.path.exists(os.path.join(env["RUNE_HOME"], "registry", "geometry", "0.2.0", "Rune.toml")))
        write(os.path.join(app, "src/main.rune"),
              "import std::io\nimport shapes\nimport geometry\n"
              "fn main() -> i64 {\n"
              "    let c = shapes::Circle { centre: geometry::Point { x: 0.0, y: 0.0 }, radius: 2.0 }\n"
              "    io::println(shapes::contains(c, geometry::Point { x: 1.0, y: 1.0 }))\n    0\n}\n")
        out = run(["run"], app, env)
        check("the app builds against the installed packages and runs", "true" in out.split(), out)
        out = run(["deps"], app, env)
        check("deps shows the tree", "shapes v1.0.0" in out and "geometry v0.2.0" in out, out)
        out = run(["installed"], app, env)
        check("installed shows one project each", out.count("1 project") == 2, out)

        # Conflicting requirement.
        out = run(["add", "geometry@=0.1.0"], app, env, expect=1)
        check("a conflict is reported with what was asked", "no version of 'geometry' satisfies" in out, out)

        # A requirement edited by hand that the lock's pin no longer
        # satisfies: the build resolves again rather than trusting the pin,
        # and a version that does not exist is an error, not a silent 1.0.0.
        mp = os.path.join(app, "Rune.toml")
        ms = open(mp).read()
        open(mp, "w").write(ms.replace('shapes = "1.0.0"', 'shapes = "=2.2.0"'))
        out = run(["deps"], app, env)
        check("deps flags a pin the manifest no longer accepts", "stale" in out and "=2.2.0" in out, out)
        out = run(["build"], app, env, expect=1)
        check("a hand-edited version that does not exist fails the build",
              "no version of 'shapes' satisfies =2.2.0" in out and "available: 1.0.0" in out, out)
        open(mp, "w").write(ms.replace('shapes = "1.0.0"', 'shapes = "=1.0.0"'))
        out = run(["build"], app, env)
        check("a hand-edited version that exists builds", "Finished" in out, out)
        open(mp, "w").write(ms)

        # A second registry, `mirror`, carrying its own geometry 0.3.0 and a
        # package of its own. Names tell them apart everywhere.
        mirror = os.path.join(tmp, "mirror")
        run(["registry", "init", mirror], tmp, env)
        set_version(os.path.join(tmp, "geometry"), "0.3.0")
        run(["registry", "--addPackage", "geometry", "--dir", mirror], tmp, env)
        run(["new", "colours", "--lib"], tmp, env)
        write(os.path.join(tmp, "colours/src/lib.rune"), "pub fn red() -> String { \"red\" }\n")
        run(["registry", "--addPackage", "colours", "--dir", mirror], tmp, env)
        out = run(["registry", "add", "file://" + mirror], tmp, env)
        check("a registry is added under its own name", "Added registry 'mirror'" in out, out)
        out = run(["registry", "add", nameless_named(tmp, "mirror")], tmp, env, expect=1)
        check("two registries cannot share a name", "already configured" in out and "--name" in out, out)
        out = run(["search", "--registry", "mirror", "."], tmp, env)
        check("search filters by registry", "colours" in out and "shapes" not in out, out)
        out = run(["search", "local::."], tmp, env)
        check("search takes registry::pattern", "shapes" in out and "colours" not in out, out)
        out = run(["search", "geometry"], tmp, env)
        check("search tags where each package is from", "[mirror]" in out or "[local]" in out, out)
        out = run(["desc", "geometry"], tmp, env)
        check("desc says where else a package is", "also in:" in out, out)
        out = run(["desc", "local::geometry"], tmp, env)
        check("desc restricted to a registry lists its versions only",
              "versions:     0.2.0, 0.1.0" in out and "registry:     local" in out, out)
        out = run(["desc", "local::colours"], tmp, env, expect=1)
        check("desc names the registry that has it", "it is in: mirror" in out, out)
        out = run(["installed", "--registry", "mirror"], tmp, env)
        check("installed filters by registry", "nothing from registry 'mirror'" in out, out)

        # The app takes geometry from the mirror explicitly: the manifest
        # records the registry, and the lock pins the mirror's copy.
        out = run(["add", "mirror::geometry"], app, env, expect=1)
        check("a registry-specific ask that conflicts is reported",
              "registry 'mirror'" in out and "satisfies" in out, out)
        out = run(["add", "colours", "--registry", "mirror"], app, env)
        check("add --registry records the registry in Rune.toml",
              'colours = { version = "0.1.0", registry = "mirror" }' in open(os.path.join(app, "Rune.toml")).read(), out)
        lock = open(os.path.join(app, "Rune.lock")).read()
        check("the lock names the mirror's url for colours", "file://" + mirror in lock, lock)
        out = run(["deps"], app, env)
        check("deps tags each dependency's registry", "[mirror]" in out and "[local]" in out, out)
        out = run(["deps", "--registry", "mirror"], app, env)
        check("deps --registry lists what came from it", "colours" in out and "shapes" not in out, out)
        out = run(["installed"], app, env)
        check("installed tags registries once there are two", "[mirror]" in out and "[local]" in out, out)
        out = run(["update", "--registry", "local"], app, env)
        check("update --registry holds the other registry's packages", "colours" not in out, out)
        out = run(["remove", "local::colours"], app, env, expect=None)
        check("remove checks the registry named", "not from 'local'" in out, out)
        out = run(["remove", "mirror::colours"], app, env)
        check("remove accepts registry::name", "Removed colours" in out, out)

        # Documentation of an installed package, and of one not installed
        # yet. Neither build references anything: the store is not a project.
        out = run(["remove"], tmp, env)
        check("the dependency nothing uses any more is reclaimed", "Uninstalled colours v0.1.0" in out, out)
        out = run(["doc", "shapes", "--no-open"], app, env)
        check("doc <package> builds the package's documentation", "Documentation at" in out and "shapes/1.0.0" in out, out)
        out = run(["doc", "mirror::colours", "--no-open"], tmp, env)
        check("doc registry::package fetches what is not installed", "Installed colours v0.1.0" in out and "Documentation at" in out, out)
        out = run(["doc", "nothing-here", "--no-open"], tmp, env, expect=1)
        check("doc of an unknown package says so", "no registry has a package called 'nothing-here'" in out, out)
        out = run(["installed"], tmp, env)
        check("a package read for its docs is installed but unused", "colours v0.1.0" in out and "unused" in out, out)
        check("a doc build in the store references nothing", out.count("1 project") == 2, out)

        # A package that insists on a registry carries that into the index.
        run(["new", "palette", "--lib"], tmp, env)
        write(os.path.join(tmp, "palette/src/lib.rune"), "import colours\npub fn first() -> String { colours::red() }\n")
        pp = os.path.join(tmp, "palette/Rune.toml")
        ps = open(pp).read().replace("[dependencies]\n", '[dependencies]\ncolours = { version = "0.1", registry = "mirror" }\n')
        open(pp, "w").write(ps)
        run(["registry", "--addPackage", "palette", "--dir", reg], tmp, env)
        index = open(os.path.join(reg, "index.toml")).read()
        check("the index records the registry a dependency insists on", 'dependencies = ["mirror::colours 0.1"]' in index, index)
        out = run(["add", "palette", "--registry", "local"], app, env)
        check("a transitive registry-specific dependency resolves there", "Installed colours v0.1.0" in out or "colours" in open(os.path.join(app, "Rune.lock")).read(), out)
        run(["remove", "palette"], app, env)

        # Dropping the mirror: what came from it stays installed.
        out = run(["registry", "remove", "mirror"], tmp, env)
        check("server remove drops the registry and says what stays", "Removed registry 'mirror'" in out and "stays installed" in out or "stay installed" in out, out)
        out = run(["registry", "list"], tmp, env)
        check("the mirror is gone from the list", "mirror" not in out, out)
        out = run(["registry", "remove", "mirror"], tmp, env, expect=1)
        check("removing it again fails", "no registry called 'mirror'" in out, out)
        out = run(["remove"], tmp, env)
        check("unreferenced packages from the doc reads are reclaimed", "Uninstalled colours v0.1.0" in out, out)

        # A newer geometry: update moves it and the old one becomes unused.
        set_version(os.path.join(tmp, "geometry"), "0.2.1")
        run(["registry", "--addPackage", "geometry", "--dir", reg], tmp, env)
        out = run(["update"], app, env)
        check("update moves to the newest allowed", "v0.2.0 -> v0.2.1" in out, out)
        check("update warns about the unused version", "not used by any project" in out, out)
        out = run(["remove"], app, env)
        check("remove with no arguments uninstalls it", "Uninstalled geometry v0.2.0" in out, out)
        check("the unused directory is gone",
              not os.path.exists(os.path.join(env["RUNE_HOME"], "registry", "geometry", "0.2.0")))

        # A fresh machine: the lock alone is enough to build.
        shutil.rmtree(os.path.join(env["RUNE_HOME"], "registry"))
        shutil.rmtree(os.path.join(env["RUNE_HOME"], "cache", "archives"), ignore_errors=True)
        out = run(["build"], app, env)
        check("a build with only the lock reinstalls what it pins", "Installed shapes v1.0.0" in out and "Finished" in out, out)

        # Tampering is caught.
        archive = os.path.join(reg, "packages", "shapes", "shapes-1.0.0.tar")
        shutil.rmtree(os.path.join(env["RUNE_HOME"], "registry", "shapes"))
        shutil.rmtree(os.path.join(env["RUNE_HOME"], "cache", "archives"), ignore_errors=True)
        with open(archive, "ab") as f:
            f.write(b"garbage")
        out = run(["build"], app, env, expect=1)
        check("a tampered archive fails its checksum", "checksum does not match" in out, out)

        # Removing the dependency drops the references.
        run(["registry", "--addPackage", "shapes", "--dir", reg, "--force"], tmp, env)
        out = run(["remove", "shapes"], app, env)
        check("remove drops the dependency line", 'shapes = "1.0.0"' not in open(os.path.join(app, "Rune.toml")).read(), out)
        check("nothing remains pinned", "[[package]]" not in open(os.path.join(app, "Rune.lock")).read())
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    if failures:
        print("\n" + "\n\n".join(failures))
        print(f"\n{len(failures)} check(s) failed")
        return 1
    print("\nregistry: all checks passed")
    return 0

if __name__ == "__main__":
    sys.exit(main())
