# Concurrency

## The graph, not a walk

`rune` resolves every package reachable from the root **before** compiling any
of it. `resolvePackages` walks the manifests depth first, detects cycles, and
returns nodes ordered so that a package appears only after everything it
depends on.

That is what makes the build concurrent. A recursive build discovers a
dependency only when it needs it and can do nothing but wait; a graph known in
advance says which packages have nothing to do with each other.

## The steps

Each package becomes two kinds of step:

```
  P_lib(pkg)      stage dependencies' .rul, compile c-sources,
                  build the package library
                  ── depends on P_lib(d) for each direct dependency d

  P_target(pkg,t) one per binary or extra root
                  ── depends on P_lib(pkg) only
```

A dependent needs its dependency's `.rul` and nothing else, so it waits on
`P_lib`, never on that package's binaries. Four sibling libraries compile at
once; six binaries of one package compile at once as soon as its library
exists.

`runGraph` in `Jobs.cpp` runs them: an in-degree count per job, a ready queue,
a condition variable, `jobLimit()` workers. A failure stops new jobs from
starting — there is no point compiling against a library that did not build —
but lets the ones already running finish, so their diagnostics are not lost
halfway through.

## Output

A compiler diagnostic is a dozen lines with carets lined up under source text.
Two of them woven together is worse than either alone, so a step's output is
**captured and printed in one piece** when it finishes. `runCaptured` runs the
command with `2>&1` into a pipe; `writeSerialized` writes the whole block under
a mutex.

A child writing into a pipe cannot see whether the build's output is bound for
a terminal, so `runStep` passes `--color` explicitly when it is.

## Dividing the machine

`runec` threads its front end and its back end. Left alone, eight compilers
on an eight-core machine would each ask for eight threads and spend the
difference fighting over them. `runStep` sets `RUNE_JOBS=sharePerJob()`: the
cores divided by the number of steps **running when the step starts**, never
below one.

It used to divide by the job limit, which defaults to the core count. That gave
every compile one thread, even a package building alone, which is most builds
and every last link of a dependency chain.

## Shared state

Two rules the job bodies follow:

- **Write only into a slot of your own.** Executables produced by a step go into `producedExes[i][t]`, not a shared list, and the ordered list is rebuilt afterwards from the target order — so `rune run` with no argument still means the first executable the manifest names.
- **Not `std::vector<bool>`.** Its elements share bytes, so two steps finishing at once would be writing the same one. `commandTest` uses `std::vector<char>` for exactly this reason.

## What made this possible

Two bugs had to be fixed first, both invisible while the build was serial:

- `runec` extracted an imported `.rul`'s object to a **fixed** path in the temp directory. Two compiles importing the same library raced, one deleting the object while the other was still linking against it. The name now carries the process id.
- A `[[bin]]` whose file also defines `main` produced two identical targets. In a serial build the second found the first's output current and skipped; in a parallel one, both wrote the same file.

Both are the same shape of bug: something that was merely wasteful when steps
happened one at a time becomes a race when they do not.
