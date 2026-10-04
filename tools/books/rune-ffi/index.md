# rune ffi: Rune bindings for C headers

`rune ffi` does what Rust's bindgen does: it reads C headers with
**libclang** and writes Rune bindings for them. It is a Rune program
(`tools/rune-ffi`), and reaches libclang through Rune's own `extern "C"`
declarations — there is no C source anywhere.

It is the one tool the toolchain does not build: libclang is not something
every machine has. The first `rune ffi` builds it into `~/.rune/bin`, against
the libclang it finds, and every later one runs that copy. `rune tools`
says whether it has been built.

## From the command line

```sh
rune ffi /usr/include/zlib.h --link z -o src/zlib.rune
```

| Option | Does |
| --- | --- |
| `-o`, `--output <file>` | write there instead of to standard output |
| `-I <dir>`, `-D NAME[=V]`, `-U NAME` | as a C compiler takes them |
| `-l`, `--link <lib>` | `#link("lib")` at the top of the bindings |
| `--target <triple>`, `--sysroot <dir>` | bindings for another machine |
| `--allowlist-function`, `--allowlist-type`, `--allowlist-var` | only names matching a `*` pattern (`a\|b` for either) |
| `--allowlist-file <pattern>` | everything declared in matching headers |
| `--blocklist-item`, `--opaque-type` | leave out, or keep only behind pointers |
| `--clang-arg <arg>` | anything else for clang |
| `--enum-style rune` | C enums as `#Convention("C")` enums instead of constants |
| `--no-layout-tests`, `--no-cstring`, `--no-constants`, `--reserved` | turn the defaults off (or, for `--reserved`, on) |

Several headers are read as one translation unit, as they would be in a C file.

## From Rune

The work is done by the `interface` unit of `tools/rune-ffi`, which a Rune
program can use directly:

```rune
import interface

var b = interface::Builder()
b.header("/usr/include/zlib.h")
b.link("z")
match b.generate() {
    Ok(bindings) => bindings.writeTo(&"src/zlib.rune"),
    Err(why) => io::eprintln(why),
}
```

`headerText(name, text)` binds a header held in a string.

## What comes out

| C | Rune |
| --- | --- |
| `struct s { ... }` | `#Convention("C") pub struct s { ... }`, every member defaulted to zero, so `s {}` is a zeroed `s` |
| a union, a struct with bit-fields, a packed struct | its bytes — the right size and alignment — with a getter and a setter per member |
| an anonymous struct or union member | a member `anonN`, with its members forwarded from the outer struct |
| a struct only declared | `pub struct s {}`, used behind pointers |
| `typedef` | `pub type` — `size_t`, `int32_t` and friends become `usize`, `i32` |
| `enum e { A, B }` | `pub type e = u32` and `pub global A: e = 0` (or a Rune enum) |
| `#define N 42`, `"text"`, `1.5` | `pub global N: i32 = 42`, `CString`, `f64` |
| functions and `extern` variables | one `pub extern "C"` block; `const char *` parameters are `CString` |
| `void (*)(int)` | `@cfunction(i32)` |

Every struct's size, alignment and member offsets are clang's, and the
bindings end with `layoutMismatches()`, which says how many Rune disagrees
with — zero on the target they were written for.

What is written: everything the headers you name declare, and every type
that reaches — not the rest of whatever system headers they include.

## Finding libclang

The first `rune ffi` looks for libclang, in order:

1. `RUNE_LIBCLANG_DIR`, the directory holding it;
2. the directory an `llvm-config` on `PATH` reports (`llvm-config-19` and its relatives too), or Homebrew's;
3. the usual places: `/usr/lib/llvm-*/lib`, `/usr/lib`, `C:/Program Files/LLVM/bin`.

It links there, and remembers where: clang's own headers (`stdarg.h` and
the rest) are found beside it when the tool runs. `RUNE_CLANG_RESOURCE_DIR`
overrides that, and `SDKROOT` the macOS SDK. To install libclang:

| System | |
| --- | --- |
| macOS | `brew install llvm` |
| Debian, Ubuntu | `apt install libclang-dev` |
| Fedora | `dnf install clang-devel` |
| Windows | the LLVM installer from llvm.org, then `RUNE_LIBCLANG_DIR="C:/Program Files/LLVM/bin"` |

## Tests

`tools/rune-ffi/tests/bindgen.rune` checks what the generator writes; CTest
runs it as `rune_ffi` when the build machine has a libclang.
