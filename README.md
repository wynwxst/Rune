# Rune

A low-level, statically typed, compiled programming language with Swift/Rust
flavoured syntax, automatic reference counting, and *optional* memory safety.
The toolchain is written in C++20 and targets native code through LLVM.

```rune
import std::io

struct Vec2 { pub x: f64, pub y: f64 }

// `Display` is how a value prints. Every builtin is bound to it, and
// `io::println` takes anything that is.
bind io::Display to Vec2 {
    fn display(&self) -> String { "(" + self.x.$str() + ", " + self.y.$str() + ")" }
}

bind operator::add to Vec2 {
    fn add(&self, rhs: &Vec2) -> Vec2 {
        Vec2 { x: self.x + rhs.x, y: self.y + rhs.y }
    }
}

fn firstNonZero(values: [f64]) -> f64? {
    for v in values {
        if v != 0.0 { return v }
    }
    nil                                   // `nil` is `Option::None`
}

fn main() -> i64 {
    let a = Vec2 { x: 1.0, y: 2.0 }
    let b = Vec2 { x: 0.5, y: 0.5 }
    io::println(a + b)                    // (1.5, 2.5)

    let samples: [4:f64] = [0.0, 0.0, 3.5, 1.0]
    io::println(firstNonZero(samples) ?? -1.0)   // 3.5
    0
}
```

---

## What's in the box

| Directory  | Contents |
|------------|----------|
| `runec/`   | The compiler: lexer, parser, AST, type system, symbol table, semantic analysis, LLVM code generation, and the diagnostic engine. |
| `rune/`    | The package manager and build front end. Reads `Rune.toml`, resolves path dependencies, drives `runec`. |
| `runetime/`| The memory core of the runtime, **written in Rune**: allocation, reference counting, the weak-reference table and raw memory. |
| `runtime/` | The rest of the runtime, still C: the `String` object, panics, tracebacks and I/O primitives. |
| `stdlib/`  | The standard library, written in Rune: `std::io` (console, files, byte buffers and streams), `std::net` (TCP), `std::thread`, `std::option`, `std::result`, `std::math`, `std::process`, `std::mem`, `std::any`, `std::iter`, `std::text`, `std::time` (durations, a clock, and a calendar), `std::atomic`, `std::dictionary`, `std::convert`, `std::collections` (`vector`, `slice`), and the everyday things a program wants from outside itself: `std::env`, `std::random`, `std::hash`, `std::json` and `std::cli`. |
| `examples/`| Sample programs: a full syntax tour, plus one per feature area and a two-package project. |
| `tests/`   | End-to-end cases: compile, run, compare output. |
| `tools/`   | `rune-doc`, the documentation generator — a Rune program — and the stylesheet and script the pages it writes are built from. |
| `docs/`    | The language reference: generated HTML plus its source in `docs/reference/`. |

File extensions: `.rune` for source, `.rul` for a compiled library (object code
plus the module interface).

---

## Building

Requires CMake 3.20+, a C++20 compiler, and LLVM 17 or newer with its
development headers.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

The build finds LLVM through `llvm-config`; point it somewhere specific with
`-DLLVM_DIR=$(llvm-config --cmakedir)`. Binaries land in `build/bin`.

```bash
export PATH="$PWD/build/bin:$PATH"
```

---

## Quick start

```bash
rune new hello
cd hello
rune run
```

A worked two-package example lives in
[`examples/project/`](examples/project/): a `statistics` library and a
`report` binary that depends on it.

```bash
cd examples/project/report && rune run -- 1 2 3 4 5
cd examples/project/statistics && rune test
```

Or drive the compiler directly:

```bash
runec -o hello examples/kitchen_sink.rune && ./hello
```

### Package layout

```
Rune.toml            manifest
src/main.rune        binary root   -> target/<profile>/<name>
src/lib.rune         library root  -> target/<profile>/<name>.rul
src/*.rune           a `main` or @type(...) makes its own target;
                     anything else is a component, compiled into them
tests/*.rune         one test program per file
docs/                yours: every .md under it is a page
docs/target/         the built documentation
target/              build output
```

Every file under `src/` that declares an output gets one: a top-level `main` or
`@type(Executable)` becomes its own binary, `@type(Library)` its own `.rul`,
and `@type(Object | Assembly | LLVM)` an `.o`, `.s` or `.ll`. Files that
declare nothing are components — importable, and compiled into each target
that needs them.

```toml
[package]
name = "hello"
version = "0.1.0"
edition = "2025"

[build]
safety = "full"        # none | minimal | full
memory = "arc"         # arc | zombie: reference counting, or single ownership
emit = "exe"           # exe | lib | obj | asm | llvm-ir
optimize = 0
debug = true

[dependencies]
geometry = { path = "../geometry" }
```

Commands: `rune new`, `init`, `build`, `run`, `test`, `doc`, `check`, `clean`,
each accepting `--release`. `rune run <name>` picks one executable,
`rune run --all` runs every one.

### Incremental and concurrent builds

A build resolves the whole package graph before compiling any of it, then runs
the steps in an order decided only by what genuinely needs what. Two packages
that do not depend on each other are compiled at the same time, as are a
package's own binaries once its library exists, and the test programs under
`tests/`. `-j <n>` caps how many run at once; the default is one per core.

A step is skipped when a digest of everything it reads — its sources, its
dependencies' `.rul` files, the manifest, the compiler binary and the standard
library — matches the one recorded beside its output. Because it is the bytes
that are compared and not the timestamps, touching a file, checking it out
again or restoring it from a cache rebuilds nothing, and editing the standard
library rebuilds everything that uses it. `--release`, `--target` and the
`[build]` settings are part of the digest; `-v` and colour are not, so asking
for a noisier build does not cause one.

```bash
rune build -j 4        # at most four compiles at once
rune build             # second time: nothing to do, and it says so quickly
```

### Building the toolchain optimised

`runec` links LLVM statically, so the compiler's own build type shows up in
every compile it performs — about twice over, measured on this tree:

| | `Debug` | `Release` |
|---|---|---|
| Start-up, before any work | 24 ms | 14 ms |
| One `hello_world` compile | 141 ms | 67 ms |
| The end-to-end suite | 36 s | 26 s |

`RelWithDebInfo` is the default when nothing says otherwise. Reach for `Debug`
only when stepping through the compiler itself.

### Documentation

`rune doc` builds one page, `docs/target/index.html`, out of two halves that
do not know about each other:

* **what the compiler saw** — every `pub` declaration, with the `///` comments
  that were on it. Modules, types, methods, fields, variants, associated
  types, functions, macros, globals, type aliases and bindings, each rendered
  beneath whatever it belongs to rather than on a page of its own;
* **what you wrote** — every `.md` file under `docs/`. There is no naming
  convention and no front matter: a file is a page because it is there, and a
  folder's `index.md` or `README.md` becomes the heading its siblings sit under.
  An optional `docs/order.json` lists folders (and files) in the order they
  should appear; anything it does not name still appears, after, alphabetically.

`docs/` is yours. The generator only reads it, and writes nothing but
`docs/target/`. Either half may be missing — a package with no `src/` builds
its guide, and a package with no guide builds its reference.

The page itself is one file with a tree down the left. Every top-level entry
— each written page, each folder of them, each module — is a page of its own,
and one is on screen at a time. The tree follows the reader: branches open as
what they name comes into view and fold up again once it has passed, except
the ones opened deliberately. Three icons at the top of it fold the tree away,
expand or collapse everything, and search (`/` from anywhere).

Two entries may be called the same thing — a package's library and the program
it builds are both `json` — so a mark beside the name says which, rather than a
`__bin_main` suffix bolted onto it.

`rune doc --open` shows the page once it is written. The standard library has
the same treatment, kept in the cache and rebuilt when the library changes:
`rune doc std::io` opens its reference at that module, from any directory,
and each module's page starts with a written guide — `stdlib/docs/std/<module>.md`,
whose examples are compiled and run by `tools/check_stdlib_docs.py`.

### Packages from a registry

A dependency is a path, or a version from a registry:

```toml
[dependencies]
geometry = { path = "../geometry" }
shapes = "1.0"                                   # ^1.0: the newest 1.x
logger = { version = "1.0", registry = "lab" }   # from that registry alone
```

A registry is a directory of static files — an `index.toml` listing every
release with its checksum and dependencies, and a `packages/` tree of plain
`tar` archives — so serving one is serving files, and a registry on a shared
drive needs no server at all:

```bash
rune pkg init registry --name work                  # make one; the name is what clients call it
rune pkg server --addPackage ../geometry --dir registry
rune pkg server --serve --dir registry              # http://localhost:7878
rune pkg server add http://localhost:7878           # on the machines that use it: added as 'work'
rune pkg server add http://other:7878 --name lab    # under an alias, when names would clash
rune pkg server list                                # what this machine uses; `remove <name>` drops one

rune search 'geo|shape'      # regex over names and descriptions; --registry lab to look in one
rune desc geometry           # versions, authors, dependencies, installed? — and which other registries have it
rune add geometry@0.2        # install, write Rune.toml, pin in Rune.lock
rune add lab::geometry       # the same, from that registry alone; the manifest records it
rune update                  # newest versions the requirements allow
rune deps                    # the tree, each package tagged with its registry
rune remove geometry         # drop it; `rune remove` alone uninstalls what nothing uses
rune installed               # every installed version, who uses it, and where it came from
rune doc geometry            # its documentation — the copy this project uses, or the newest
```

A requirement is Cargo's: `"1.2"` is `^1.2` (>=1.2.0 <2.0.0), `"0.3"` is
>=0.3.0 <0.4.0, `"~1.2"` is >=1.2.0 <1.3.0, `"=1.2.3"` is exactly that,
`">=1.2, <2.0"` is every comparison listed, `"*"` is anything. One that
nothing satisfies — `stats = "=2.2.0"` when there is no 2.2.0 — is an error
from `rune add` and from the next `rune build`, whatever the lock says.

Installed packages live under `~/.rune/pkg/<name>/<version>/`, once each
however many projects use them, and each keeps the list of projects that
reference it. `Rune.lock` pins what a project resolved; commit it, and a build
elsewhere fetches the same versions. Resolution needs only the index, so every
missing archive downloads at once, is checked against its checksum, and is
unpacked into place whole. Every registry has a name — the one its index
declares, or the alias it was added under — and `<registry>::<package>` or
`--registry <name>` names one wherever a package is named.

The reference's *Packages and registries* section walks through making a
package, using one and running a registry, and
[`examples/package/`](examples/package/README.md) is all of it worked
through: seven packages with a dependency diamond and two versions of two of
them, two programs, two registries built from them — one carrying its own
`logger`, which an app asks for by name — and `ecosystem.py`, which builds,
serves, or proves the whole cycle under a temporary `RUNE_HOME`.

`--emit <kind>` builds the package's roots into something other than an
executable — `rune build --emit llvm-ir` leaves one `.ll` per root under
`target/<profile>/` and links nothing. `llvm-ir`, `asm`, `obj`, `lib` and
`exe` are the spellings, and `[build] emit` says the same thing when the
command line does not. Dependencies still build as libraries, because that is
what the package needs from them in order to compile at all.

Dependencies are built before whatever needs them, their `.rul` files are
staged into `target/<profile>/deps/` (transitively, so a package only names
what it uses directly), native link flags propagate up the graph, and any step
whose output is already newer than its inputs is skipped.

---

## Memory model

Rune uses **automatic reference counting**, inserted by the compiler.

* **Value types** — integers, floats, `bool`, `Character`, structs, enums,
  tuples and arrays — are copied.
* **Reference types** — classes, `String`, closure environments and mark
  objects — live on the heap behind a header and are retained and released
  automatically.
* `deinit` runs deterministically when the last reference goes away, and a
  subclass's `deinit` chains into its base.
* A `weak` field does not keep its target alive and reads as empty once the
  target is gone, which is how a reference cycle is broken after the fact.
* A `Unique<T>` field has exactly one owner and can be moved or borrowed but
  never copied, which is how a cycle is made impossible in the first place —
  closing a ring needs a second reference, and none can be produced.

There is no garbage collector and no runtime pause.

### Single ownership, without a count

Reference counting is the default, not the only choice. Built with
`--memory zombie` (or `memory = "zombie"` under `[build]`), a program keeps no
counts at all: every value the heap owns has exactly one owner, is handed on by
*moving*, and is destroyed when its owner's scope ends. A second, precise
borrow checker — **Zombie** — proves that every borrow is finished before the
value it points at is gone, so nothing dangles and nothing is freed twice.

```rune
let x = Box(1)
let a = take(x)         // `x` moves into `take`
take(x)                 // error: 'x' has been moved out of
```

It is flow-sensitive and place-based, after Rust's Polonius: borrows end at
last use, disjoint fields never clash, `v.push(v.len())` is fine. Where a
returned reference borrows from is inferred; write it down with a `from` place
clause (`-> &String from (a, b)`, `from self.text`, `from global`) only to pin
an interface. Views (`&var self { field }`) and internal references
(`body: &String from self.text`) fall out of the same idea. `weak` and the
shared-only types (`thread::Arc`) are unavailable under Zombie and say so.
`--memory arc` is reference counting, unchanged, and a library records which
mode it was built for. See the reference's *Single ownership* section for the
full story.

## Safety

Safety is a dial, not a switch. `--safety=full` (the default) inserts bounds
checks, nil checks and division-by-zero checks, and reports whatever is still
live at exit. A class that can reach itself through counted references is
reported too, with the route spelled out — as a warning, since the check works
on types and a type that can loop is not a program that does. Own a structure
through `Unique` and it cannot loop at all. `minimal` keeps nil checks and the
leak report; `none` removes them all.

Integer overflow follows the build rather than the dial: a debug build
(`-O0`, what `rune build` produces) traps on `+`, `-`, `*` and unary `-`
the way a bounds check does, and a release build wraps. `--overflow-checks`
and `--no-overflow-checks` pin it either way, and `$wrappingAdd`,
`$saturatingAdd` and `$checkedAdd` — with `Sub` and `Mul` beside each, and
the same family in `std::math` — say what an operation means at every
setting.

`full` also reads each function body for what it does with what it owns: a
borrow that would outlive the binding it points into, two borrows of the same
value where one can write, a value that owns a resource being copied out of a
place that still holds it, and a use of something that has already been handed
away. Below `full` each is a warning instead. The same pass answers a question
nothing reports — which locals never leave the scope that declared them — and
a class built for one of those needs no reference counting at all: the
allocation's own count becomes the binding's, and the scope hands it back on
the way out.

Operations the compiler cannot vouch for — raw pointer dereferences, pointer
casts, and every foreign call — produce a warning in a safe context:

```
┌ ● demo.rune [25:4..28]
│ 24 ║     n: i64 = 5
│ 25 ║     poke(&var n as *var i64)
│          ^^^^^^^^^^^^^^^^^^^^^^^^ WARNING: call to unsafe function 'poke' in a safe context [W0001]
│ 26 ║     0
│     ─  note: wrap it in `unsafe { ... }`, mark the caller @unsafe, or justify it with @safe("reason")
│
│ ○ demo.rune [24:3..7]
└▶ 'main' is not marked unsafe
  24 ║ fn main() -> i64 {
          ─────────────> hint: annotate it with @unsafe, or justify the use with @safe("reason")
```

Three ways to resolve one:

```rune
@unsafe                      // this function is unsafe; callers are warned instead
fn poke(p: *var i64) { *p = 1 }

@safe("abs is total: every i32 input has a defined result")
fn absolute(v: i32) -> i32 { abs(v) }   // justified, no warning

unsafe { printf("%s\n", text) }         // a scoped opt-out
```

---

## Language at a glance

See [`docs/rune-reference.html`](docs/rune-reference.html) for the full
reference — 29 categories, every example compiled and run by `runec` when the
page was generated. Rebuild it with:

```
python3 docs/reference/build.py
```

[`docs/language.md`](docs/language.md) is the shorter prose overview, and
[`examples/kitchen_sink.rune`](examples/kitchen_sink.rune) is a program that
exercises everything below. [`docs/roadmap.md`](docs/roadmap.md) is the other
half of the picture: what the language does *not* do yet, what other languages
do about it, and which of their answers would fit here.

* Optional semicolons; line ends terminate statements
* Immutable bindings by default (`x = 7` or `let x = 7`), `var` to opt in
* Blocks and functions return their last expression
* `i8`–`i64`, `u8`–`u64`, `isize`, `usize`, `f32`, `f64`, `bool`,
  `Character`, `CString`, `String` (plus `int`, `uint`, `float`, `double` and
  `Byte`, which is `u8` under the spelling the other named builtins use)
* Arrays `[5:i64]` with constant-expression lengths, slices `[i64]` and
  slicing `values[a..b]`, tuples `(i64, String)`
* `Option<T>` and `Result<T, E>` as real enums; `T?`, `nil`, `??` and `?` are
  sugar over them
* Structs, classes with single inheritance, Rust-style enums with payloads
* Marks (traits) with default methods and super-marks; `bind Mark to Type`,
  including binds to builtin types
* `Self` in a mark: static requirements that construct, chosen by the type in
  context — `let d: Sheep = Animal::new("dolly")`
* A type's own methods win `value.name()`; the mark's stays reachable as
  `value::Mark.name()`
* `dyn Mark` mark objects for runtime polymorphism
* `for` walks ranges, arrays and slices natively, and anything else through the
  `Iterator` mark (`type Item`, `fn next(&var self) -> Self::Item?`) or the
  `Sequence` mark that hands one out. Both are in the prelude. `for v in vec`
  and `for c in text::chars(s)` come with the library
* The `for` binding is a pattern, so a value arrives already taken apart:
  `for (x, y) in cells`, `for Pair { key, value } in entries`, `for _ in xs`
* `map`, `filter` and `zip` on every iterator and every container, lazily:
  `people.filter(isAdult).map(name)` walks the source once and stores nothing.
  `iter::counting(0)` numbers things, `vector::collect(it)` runs a chain into a
  `Vector`, `I::Item` names what a cursor yields and `I::Item::Item` the item
  of that — which is what `flatten` and `flatMap` are built on
* `fn evens() -> some Iterator { ... }`: a result whose concrete type the body
  decides and callers never see. There is no box and no table — the value is
  the concrete one — so a function can hand back an iterator chain without
  spelling out `Take<Filter<Counter>>`. `some Shape` works the same way for
  any mark
* `Any` holds a value of any type and gives it back only as the type it really
  is: `boxed.typeName()`, `boxed is i64`, `boxed.get::<i64>()` (an `Option`),
  `boxed.expect::<i64>()`. The answer comes from a descriptor in the value's
  own object header, so it is one pointer comparison and is right across
  separately compiled packages
* String literals are interned: one immortal object per literal, so evaluating
  one costs no allocation and it never appears in the live-object count
* Compiler-provided members carry a `$` (`text.$length()`, `n.$str()`), so they
  never collide with a method a type declares
* `std::mem` — `size_of`, `align_of`, `is_counted<T>()`, an `Allocator` mark,
  and `Handle<T>`, a reference-counted smart pointer;
  `std::collections::vector` is the growable array built on them
* `std::mem::Buffer<T>(n)` is a bounds-checked block sized at run time, with
  `b[i]` and `b[i] = v`; leave the fill out and it zeroes
* `std::collections` — arrays, slices and `Vector<T>`; `slice::` covers the
  first two, since an array converts to a slice on its own
* `std::dictionary` — `Map<K, V>` and `Set<T>`, open-addressed and keyed
  structurally: `mem::hash` and `mem::equals` work a key out from its layout,
  so any type at all can be one — no `Hashable` mark, no bound
* Every container subscripts: `v[i]`, `m[key] = value`, `s[value]` for
  membership; `Map` walks as `(key, value)` pairs and `Set` as its members
* A bare `T` or `E` promotes into `Result<T, E>`, so a function returning one
  can write `value` and `error` directly instead of `Ok(...)` and `Err(...)`
* `Option<T>` and `Result<T, E>` are ordinary enums with an ordinary API:
  `map`, `mapOr`, `andThen`, `filter`, `otherwise`, `zip`, `orElse`, `take`,
  `replace` on one; `map`, `mapErr`, `andThen`, `recover`, `unwrapErr`, `ok`,
  `error` on the other, with `result::from` bridging a `nil` into a named
  failure
* `std::math` — libm wrapped and named, the constants, `least` / `greatest` /
  `clamp` bounded on `operator::cmp` so they work for anything ordered, and
  the integer answers a float cannot give exactly: `powerInt`,
  `squareRootInt`, `gcd`, `lcm`, `divFloor`, `modFloor`
* `std::testing` — `equal`, `notEqual`, `isTrue`/`isFalse`, `isSome`/`isNone`,
  `unreachable`, `counts`, `summary`. A test is an ordinary program: each check
  prints its own line, `summary()` returns what `main` should, and `rune test`
  reads the exit status
* `std::collections::buffer` — `Buffer<T>(size, fill)`, a fixed-size block
  where every read and write is bounds checked, with `b[i]` and `b[i] = v`
* `std::io` files — `open` / `create` / `append`, `readLine`, `readAll`,
  `write`, `readToString`, `exists`, `delete`, each failure a `FileError`
* `std::io` directories — `readDirectory` lists one, `walkDirectory` walks a
  whole tree, `isDirectory` asks, `makeDirectory`/`makeDirectories` create,
  and `joinPath` puts exactly one separator between two pieces
* `std::io` streams — `Bytes` is a growable byte buffer, `Reader` and `Writer`
  are the two marks everything else is written against, `Stream` is both, and
  `BufferedReader` turns either into lines. A file becomes one with
  `file.asStream()`
* `std::net` — `listen` and `connect` give a `TcpListener` and a `TcpStream`,
  and a `TcpStream` *is* an `io::Stream`. Both own their descriptor: the
  `deinit` closes it, so a connection closes itself, and `release` / `adopt`
  are how one is handed somewhere the compiler cannot follow
* A `deinit` on a struct or an enum, written in the body, in an `extend` or in
  a `bind`, runs when the value it lives in is destroyed — so a value can own
  a descriptor or a lock, not only memory. Handing one on is a move
* `@resource` on a field says `deinit` has to release it, and `--safety full`
  holds the type to that
* Operator overloading via `bind operator::add to Type`, or the punctuation
  itself: `bind operator::"*" to Handle<T>` — overloaded once per right-hand
  type, so `money + rate` and `money + 7` can both exist, and a builtin can be
  taught a new pair (`text += 'a'`) without its own meanings being replaced
* A `bind` may supply one name more than once, so long as the versions take
  different things — `bind operator::"[]" to Value` written twice, once for a
  `String` key and once for an `int`, is two ways to index one type, and
  `indexSet` follows whichever `index` the read chose. The same goes for any
  mark requirement, so `cart.add("pear")` and `cart.add(200)` can both exist.
  A `fn` may still not be declared twice: a `bind` is the only place in the
  language where one name has several bodies. The call picks by argument type
  — exact before widening — and a call two versions accept equally well, or
  none accepts, is reported with every candidate listed. Labels and defaults
  work as usual, so a version with a defaulted tail answers the shorter call
* A `bind` target may be a shape rather than a declaration —
  `bind<T> Display to [T] where T: Display` covers every slice and every
  array of something printable, which is how `io::println([1, 2, 3])` works
* Where more than one `bind` applies, the narrower one wins: a written-out
  target beats a parameterised one, more of the shape pinned down beats less,
  and more asked of the parameters beats less. Two that are equally specific
  are reported rather than decided by declaration order — unless they take
  different parameters, which makes them overloads rather than rivals
* A mark requirement may carry type parameters and a `where` clause of its
  own, including one about the mark's associated type
  (`fn render(&self) -> String where Self::Item: Display`)
* `@alias("name")` gives any declaration a second, string-based name
* `@as("name")` renames a foreign declaration for Rune's side only, so a
  program can import C's `bind` and still declare a `bind` of its own
* Iterator adaptors: `map`, `filter`, `zip`, `take`, `skip`, `take_while`,
  `skip_while`, `enumerate`, `chain`, `step_by`, `inspect`, and `collect`,
  `count`, `find`, `any`, `all`, `fold`, `reduce`, `forEach`, `last`, `nth`,
  `position`, `best` to end a chain. `as_iter()` starts one from a container
* Two enums may claim the same variant name, and so may a variable — a bare
  mention that more than one enum answers to has to say which
* `deref` / `derefSet` make a value behave like a pointer — `*h`, `*h = v`,
  `*h += v`
* Marks with associated types: `type Item` in the mark, `type Item = i64` in
  the bind, `Self::Item` in between
* Bounds that name an operator: `fn largest<T: operator::cmp>(a: T, b: T)`
* `while value is Some(v) { ... }` destructures each turn
* `@link("m")` / `@linkpath("/opt/lib")` at the top of a file
* `-g` emits DWARF and prints a demangled traceback when a program aborts
* `@type(Executable | Library | Object | Assembly | LLVM)` at the top of a file
  says what it produces; a `main` implies an executable
* `@cfunction(T) -> U` is a bare C function pointer, and a declared function
  can become one with `as`
* `Unique<T>` is a reference with exactly one owner: `head.next = second`
  hands it over, `&head` borrows it, and nothing can copy it — so a list or a
  tree owned this way cannot form a reference cycle at all
* Closures copy their captures; `move ||(...)` says so, and assigning to a
  capture is diagnosed rather than silently lost
* `std::text` — NFC/NFD normalisation, simple case folding, and a root
  collation that sorts `apple APPLE Ápple äpple Banana zebra`, plus the
  everyday half: `split`, `splitLines`, `join`, `trim`, `replace`,
  `startsWith`, `endsWith`, `contains`, `find`, `padStart`, `padEnd`
* `bind Celsius into Fahrenheit` defines a conversion, reached with
  `c into Fahrenheit` — the same as `bind As<Fahrenheit> to Celsius`; `as`
  keeps its built-in meanings and no binding changes them
* User-defined decorators: a function whose last parameter is a function, so
  `@route("GET", "/health") fn health()` calls `route("GET", "/health", health)`
  once before `main`
* Macros: `macro name { (pattern) => { expansion } }`, invoked `name!(...)`,
  with fragments, repetition and `stringify!`. Private to their file unless
  written `pub macro`; the standard library's are public, so `vec!(1, 2, 3)`
  and `assert!(x > 0)` — which names the check after the expression — work
  anywhere
* Generics with monomorphisation and mark bounds
* Closures `||(a: i64) -> i64 { ... }` and function types
  `@function(i64) -> bool`; without the arrow, `@function(i64)` returns `()`
* `if` / `elif` / `else` / `while` / `loop` / `for` / `match`, all expressions
* Patterns with bindings in `|` alternatives, guards, ranges and destructuring,
  including slices and arrays: `[first, ..rest]`, `[.., last]`, `[a, b, c]`
* `?` converts the error on the way out: a `bind Theirs into Ours` is all a
  layered error type needs, and `process::panic` is `-> Never`, so it can end
  a function that promised a value
* Labelled loops with `break :label`
* Decorators: `@unsafe`, `@safe("...")`, `@inline`, `@export("name")`
* FFI: `extern "C" { printf(text: CString, ...) -> i32 }`
* Modules with `pub` visibility, `import a::b`, `import a::{b, c}`, `import a::*`
* A fully qualified path needs no import — `std::io::println("hi")` just works;
  `import std` brings the whole standard library in under its short names
* A package's binary compiles as `<pkg>__bin_<file>`, so `import <pkg>` from a
  binary reaches the package's own library rather than the binary itself

---

### Conditional compilation and inline assembly

`@Config(...)` decides whether a declaration exists at all. It is answered
before anything is checked, so what it rules out may name types and foreign
symbols that exist on no other target.

```rune
@Config(family == "unix")
fn lineEnding() -> String { "\n" }

@Config(family == "windows")
fn lineEnding() -> String { "\r\n" }
```

Conditions compare `os`, `arch`, `family`, `pointer_width`, `endian`, `target`,
`safety` and `opt_level` against a string, or test a name set by `--cfg`, by
`[build] cfg`, or by being a dependency — joined with `&&`, `||` and `!`.
Fields, methods, variants and `extern` declarations may each carry one.

`std::asm` hands instructions to the assembler as written:

```rune
@Config(arch == "aarch64")
fn addUp(a: i64, b: i64) -> i64 {
    unsafe { asm::value<i64>("add $0, $1, $2", "=r,r,r", a, b) }
}
```

`asm::run` is for instructions written for their effects and is never elided;
`asm::value<R>` is treated as a pure function of its inputs. Both are `@unsafe`,
and both want a `@Config` around them.

---

## Compiler reference

```
runec [options] <input.rune>...

  -o <path>          output path
  -c                 emit an object file
  --emit-llvm        emit textual LLVM IR
  --emit-asm         emit target assembly
  --emit-lib         emit a Rune library (.rul)
  --emit-docs        emit a documentation sidecar (.rdoc)
  --check            type-check only
  -O0 -O1 -O2 -O3    optimisation level
  --safety <level>   none | minimal | full
  --memory <mode>    arc | zombie (reference counting, or the Zombie checker)
  --overflow-checks  trap on integer overflow (the default at -O0)
  --no-overflow-checks
                     wrap instead (the default at -O1 and above)
  -I <dir>           module search path (.rul libraries)
  -L <dir> -l <name> native library search path and libraries
  --cfg <name>       set <name> for `@Config(...)`
  --dump-tokens      token stream
  --dump-ast         parse tree
  --dump-symbols     resolved module scope
  --time             how long each stage took
```

The compiler lexes, parses and ownership-checks its input on as many cores as
it can see; `RUNE_JOBS` says how many, and `rune` sets it so that several
compiles running at once still add up to one machine's worth. The ownership
pass reports in the order its bodies were queued rather than the order threads
finished, so its diagnostics are identical however the work was divided.

An artefact carries only the parts of the standard library it reaches. The
whole of it is read, checked and available — that is what makes a `bind`
written anywhere apply everywhere — but a function no call, vtable, initialiser
or piece of metadata in the module refers to is not lowered, and what is left
over is dropped before the back end sees it. A hello-world object goes from
918 functions to 17.

Set `RUNE_DEBUG_RC=1` when running a compiled program to poison freed objects
and get a precise report if reference counting is ever violated.

---

## Working on the toolchain

[`book/toolchain/`](book/toolchain/) documents the compiler itself: the
pipeline phase by phase, recipes for adding a keyword, syntax, a type rule, an
attribute, an intrinsic, a flag or a diagnostic, how the build decides what to
do, and the performance invariants that are easy to break by accident. Build it
with `rune doc -C book/toolchain`.

The language reference is generated: edit `docs/reference/content.py`, then
`python3 docs/reference/export_markdown.py`, then `rune doc -C docs`. Never
edit `docs/docs/**` by hand — the export rewrites it.

---

## Design notes

**Four-pass semantic analysis.** Declarations are collected, then shapes
(fields, superclasses, mark hierarchies), then signatures, then bodies. That is
what lets declarations appear in any order — no headers, no forward
declarations.

**Generics are monomorphised on use.** A template's body is checked once per
instantiation, with the failing call site attached to the diagnostic as a
related location, so an unmet bound points at both the call and the parameter
that required it.

**Libraries carry their source.** A `.rul` holds the compiled object plus the
module's Rune source. Importers re-parse it for signatures and generic bodies
while the compiled code comes from the object, and non-`pub` items stay
invisible.

**Diagnostics are structured, not printf'd.** Every message can carry notes and
related locations, and the renderer draws them as one connected box.
