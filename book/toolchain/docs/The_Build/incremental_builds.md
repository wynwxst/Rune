# Incremental builds

A step is skipped when nothing that could change its result has changed. That
is decided by a **content digest**, not by timestamps.

## What goes into a digest

`stepFingerprint(cmd, inputs)` in `main.cpp` mixes in:

- the exact `runec` command line — which covers the flags, the module name, the `-I` paths and which files go in;
- the **contents** of every source file;
- the contents of every `.rul` the step imports;
- the package manifest;
- the toolchain digest: the compiler binary's size and modification time, plus the contents of every `.rune` file in the standard library.

The result is written to `target/<profile>/.fingerprints/<name>.<hash>`, and
only after the step succeeds — so a failed build leaves the previous digest, and
therefore the previous verdict, in place.

## Why contents and not timestamps

Timestamps answer "has anything been written", which is a different question
and wrong in both directions. Touching a file, checking it out again, copying
the tree or restoring it from a cache all rewrite the timestamp without
changing a byte; a change that lands with an older timestamp than the output
goes unnoticed.

## Why the toolchain is an input

The standard library is compiled from source into every artefact, so it is an
input to all of them. Under a timestamp scheme over the package's own sources,
editing `stdlib/` or rebuilding `runec` left every package in the tree looking
up to date while none of it was — which is exactly the situation you are in
while working on the compiler.

Hashing the compiler binary's bytes would mean reading 185 MB per step, so it
contributes its size and modification time instead. Both change whenever it is
rebuilt.

The toolchain digest is computed once per `rune` process and cached behind a
mutex, since it is the same for every step.

## One stamp file per output

Rather than one index for all of them. Several targets are compiled at once, so
separate files need no lock between them and cannot leave a half-written index
behind if a build is interrupted.

The stamp's name is the output's filename plus a hash of its full path, so two
outputs that share a basename in different directories do not collide.

## The flag trap

Anything added to the fingerprinted command line becomes a rebuild trigger. So
flags that only change how the build *narrates* itself must not go in it:
`-v`, `--color` and `RUNE_JOBS` are appended in `runStep` at spawn time, not in
`appendBuildFlags`.

Before this was separated, `rune build -v` rebuilt everything and a plain
`rune build` afterwards rebuilt it again, because the two commands differed by
one flag. There is a comment in `appendBuildFlags` marking the boundary.

## `rune test`

Test programs are fingerprinted the same way and rebuilt only when changed —
but they are always *run*. A test whose binary is current is still executed,
because the point of running tests is to run them.
