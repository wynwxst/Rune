# runec

| Flag | Does |
| --- | --- |
| `-o <path>` | output path |
| `-c` | emit an object file and stop |
| `--emit-llvm` | emit textual LLVM IR |
| `--emit-asm` | emit target assembly |
| `--emit-lib` | emit a `.rul` library |
| `--check` | type-check only, produce nothing |
| `-O0` … `-O3` | optimisation level, default `-O0` |
| `-g` | emit debug information |
| `--target <triple>` | cross-compile; see **Cross compilation** |
| `--cc <program>` | the toolchain driver used to link |
| `--sysroot <dir>` | the target's headers and libraries |
| `--runtime-dir <dir>` | where `libruneruntime.a` is |
| `--link-arg <arg>` | appended to the link command verbatim |
| `--safety <level>` | `none`, `minimal` or `full` (default) |
| `--memory <mode>` | `arc` (default) or `zombie`; see **Single ownership without a count** |
| `-I <dir>` | add a module search path |
| `-L <dir>` / `-l <name>` | native library path / library |
| `--module <name>` | set the module name |
| `--cfg <name>` | set *name* for `@Config(...)` |
| `--stdlib <dir>` | where the standard library lives |
| `--no-stdlib` | do not import it implicitly |
| `-Werror` / `-w` | warnings as errors / silence warnings |
| `--error-limit <n>` | stop after *n* errors, `0` for unlimited |
| `--color` / `--no-color` | force colour on or off |
| `--dump-tokens` | print the token stream |
| `--dump-ast` | print the parse tree |
| `--dump-symbols` | print the symbol table |
| `--dump-types` | print the type of every expression |
| `-v` | report each pipeline stage |
| `--time` | report how long each stage took |

| Environment | Does |
| --- | --- |
| `RUNE_JOBS` | threads to lex, parse and check with; default one per core |
| `RUNE_CC` | the link driver, when `--cc` does not name one |

```sh
$ runec -o hello hello.rune                     # compile and link
$ runec --check src/*.rune                       # just type-check
$ runec --emit-llvm -O2 -o hot.ll hot.rune       # look at the IR
$ runec --safety none -O3 -o fast bench.rune     # no checks at all
$ runec --dump-ast small.rune | head -40         # see the parse tree
$ runec --time -o hello hello.rune               # where the time went
```
