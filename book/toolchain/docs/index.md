# The Rune toolchain

How the compiler and the build system are put together, how they work, and how
to change them.

> This is a book about the *toolchain*, not about the language. If you want to
> learn Rune, read the reference. If you want to add a keyword to it, fix a
> diagnostic, or understand why a build did what it did, you are in the right
> place.

Six chapters, and they are meant to be read roughly in order the first time.

| Chapter | Covers |
| --- | --- |
| **Orientation** | What the pieces are, how to build them, where the code lives |
| **The compiler** | The pipeline, phase by phase, from bytes to an executable |
| **Extending the language** | Recipes: a keyword, syntax, a type rule, an attribute, an intrinsic, a flag, a diagnostic, a standard-library function |
| **The build** | `rune`: the package graph, incremental builds, concurrency, cross compilation |
| **Performance** | Where the time goes, how to measure it, and the invariants that keep it fast |
| **Testing and documentation** | The suite, and how the reference is generated |

## The shortest possible summary

`runec` compiles a whole program at once — every file, plus the whole standard
library, plus the source carried inside every imported `.rul` — into one symbol
table and one LLVM module. It reads all of that, and emits only the part the
artefact actually reaches.

`rune` never compiles anything itself. It resolves the package graph, decides
which steps are still needed by comparing content digests, and runs the ones
that are, as concurrently as their dependencies allow.
