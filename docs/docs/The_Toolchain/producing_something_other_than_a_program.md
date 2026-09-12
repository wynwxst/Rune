# Producing something other than a program

`--emit` builds every root the package owns into the asked-for form instead of linking it: one file per root under `target/<profile>/`, named after the root. It is what to reach for when the question is *what did the compiler make of this* rather than *does it run*.

```sh
$ rune build --emit llvm-ir     # target/debug/<name>.ll, per root
$ rune build --emit asm         # .s
$ rune build --emit obj         # .o
$ rune build --emit lib         # .rul, even for a package with a main
```

Dependencies still build as libraries whatever `--emit` says, because a `.rul` is what this package needs from them in order to compile at all — an `.ll` in its place would leave nothing to import. A package that also produces a library keeps producing it: the emitted file is the same code in another form, not a replacement for the artefact a dependent links against.

> [!NOTE]
> **Three ways to ask**
>
> `[build] emit` in the manifest says the same thing when the command line does not, and `@type(...)` on a single file says it for that file alone. The command line wins over both.
