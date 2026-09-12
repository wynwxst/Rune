# How a build decides what to do

`rune` resolves every package reachable from the root before it compiles any of them, and turns the result into steps ordered only by what genuinely needs what. A package's library is one step; each binary it produces is another, waiting only on that library; a dependent's library waits on the library it depends on and on nothing else.

Steps with no edge between them run at the same time. Two sibling dependencies are compiled together, six binaries of one package are compiled together once its `.rul` exists, and so is every program under `tests/`. A step's output is collected and printed in one piece when it finishes, so two compiles failing at once produce two diagnostics rather than one interleaved mess.

A step is skipped when a digest of everything it reads matches the one recorded beside its output: the command line, the contents of every source file, the contents of every `.rul` it imports, the manifest, the compiler binary, and every source file of the standard library.

> [!NOTE]
> **Why the digest, rather than mtimes**
>
> Contents, not timestamps. Touching a file changes nothing, and neither does checking it out again, copying the tree or restoring it from a cache — while a change that arrives with an older timestamp than the output is still noticed. The standard library counts as an input because it is compiled from source into everything, so editing it rebuilds what used it.

```sh
$ rune build
○ Compiling geometry v0.1.0 (library)
○ Compiling app v0.1.0
● Finished debug profile

$ rune build              # nothing has changed
● Finished debug profile

$ touch src/main.rune
$ rune build              # still nothing: the bytes are the same
● Finished debug profile
```

Flags that change what is produced are part of the digest, so `--release`, `--target` and the `[build]` settings each get their own answer. Flags that only change how the build narrates itself are not, so `rune build -v` after `rune build` recompiles nothing. `rune clean` deletes `target/`, digests included.
