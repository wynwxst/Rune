# Rune: what is missing, and what might come next

This is a design document, not a promise. It exists to write down what the
language does not do yet, what other languages do about those things, and
which of their answers would fit Rune — so that a decision made later is made
against something rather than from scratch.

Every gap named here was checked against the compiler in this repository, not
remembered. Where a diagnostic is quoted, it is one `runec` actually prints.

Three things shape every entry below.

* **The language has no runtime.** No garbage collector, no scheduler, no
  reflection metadata beyond a type descriptor in an object header. A feature
  that needs one of those is not a feature Rune can have cheaply, and saying
  so is part of the answer.
* **Safety is a dial.** `--safety full` inserts checks and runs a borrow and
  move analysis; `none` removes them. Anything added has to have a sensible
  meaning at every setting, not just the strictest one.
* **Nothing is magic.** `T?`, `nil`, `??` and `?` are sugar over ordinary
  enums; `for` is sugar over two ordinary marks. A proposal that cannot be
  written as a library plus a spelling is a proposal to make the language
  bigger, and should say so.

---

## Where Rune stands

Present and working: automatic reference counting with `weak` and `Unique<T>`;
generics by monomorphisation with mark bounds; marks with default methods,
super-marks, associated types, conditional bindings, structural bindings over
shapes, and specificity ordering between them; `dyn Mark` objects; operator
overloading; enums with payloads and exhaustive `match`; closures; declarative
macros; user-defined decorators; C FFI; `Any` with a real type descriptor;
compile-time reflection; threads, atomics, mutexes and channels; deterministic
`deinit` on values as well as classes; cross compilation; a package manager, a
test runner and a documentation generator.

As of this revision, one further thing: **a `bind` may supply one name several
times over, told apart by what each version takes.** That is the language's
only overloading, and the rest of this document is written knowing it exists.

Since then the whole of the near-term list below has landed, together with
`some Mark` from the medium list. Each entry keeps its reasoning and now ends
with what was actually done, which is sometimes not quite what the verdict
said.

---

## Near term

Small, clearly missing, and unlikely to be controversial.

### 1. Error conversion in `?`

Today the two error types must be identical:

```
ERROR: expected 'i64' — got 'String' [E0398]
note: `?` passes the error through unchanged, so both results must carry
      the same error type
```

That is honest but it makes layered error types painful: every boundary needs
an explicit `mapErr`. Rust's answer is the `From` trait — `?` inserts a
conversion. Swift's is that every thrown value is an `Error` existential.
Go's is `errors.Is` / `errors.As` over a wrapped chain.

Rune already has the piece Rust uses: `convert::As<Target>`. So `?` could
read as "return the error, converted, if a conversion is bound":

```rune
bind As<SaveError> to io::FileError {
    fn convert(&self) -> SaveError { SaveError::Disk(*self) }
}

fn save(path: String) -> Result<(), SaveError> {
    let f = io::create(path)?      // FileError converts on the way out
    ...
}
```

The cost is one lookup at each `?` and no runtime cost at all, since `convert`
is a static call. The risk is that a conversion bound for one reason silently
changes what `?` does somewhere else, which argues for requiring the
destination to be written on the function rather than inferred — which it
already is, in the return type.

**Verdict: do it.** It is the highest ratio of pain removed to language added
in this list.

**Done.** `?` hands the error out as the function's own error type: the same
type passes through, one that converts implicitly is widened, and one with a
`bind As<Ours> to Theirs` — spelled `bind Theirs into Ours` — goes through its
`convert`. The destination is the declared result, as argued above.

### 2. Slice and array patterns

`match` handles tuples, structs and enums. It does not handle a run of
elements:

```
ERROR: Expected 'a pattern' — Got: '[' [E0112]
note: patterns are literals, `_`, bindings, tuples, or enum/struct shapes
```

Rust, Swift and most ML descendants all destructure sequences. The shape is
well understood: fixed patterns `[a, b, c]`, a rest binding `[first, ..rest]`,
and a middle rest `[first, .., last]`. For a fixed-length array the length is
known and the match is exhaustive; for a slice it is a length check followed
by the same code.

This is a parser and pattern-checker change with no type-system consequences
and no runtime cost beyond the length comparison a hand-written `if` would
have made anyway.

**Verdict: do it.** It pays for itself in parsing code alone.

**Done.** `[a, b, c]`, `[first, ..rest]` and `[first, .., last]`, in `match`,
`let`, `for` and `is`. A slice is matched by length and a `match` over one is
exhaustive when every length has an arm; against a fixed array the pattern is
irrefutable when it accounts for every element, and an error when it cannot.

### 3. Checked integer arithmetic

`i8` at 127 plus 1 is -128 today, at every safety level. Bounds checks, nil
checks and division by zero are all inserted at `--safety full`; overflow is
not, which is an inconsistency rather than a decision.

Rust traps on overflow in debug builds and wraps in release. Swift traps
always, with `&+` for the wrapping one. Zig separates `+` from `+%` in the
type system.

The dial Rune already has answers this cleanly: **overflow traps at `full`,
wraps at `minimal` and `none`**, which is exactly the shape of every other
check in the language. The explicit spellings — a `wrapping`, `saturating`
and `checked` family in `std::math`, returning `T`, `T` and `T?` — should
exist regardless, because "I meant it to wrap" deserves to be sayable.

**Verdict: do it, together.** Neither half is much use without the other.

**Done, on a different axis.** Overflow follows the *build* rather than the
safety dial — trap at `-O0`, wrap at `-O1` and above, which is what `rune
build` and `rune build --release` produce — with `--overflow-checks` and
`--no-overflow-checks` to pin it, and `overflow-checks` under `[build]`.
The reasoning is Rust's: the mistake is caught while the program is being
written and costs nothing once it is shipped, and a release build's
arithmetic should not depend on which safety level it was built at. The
named family exists as `$wrappingAdd`, `$saturatingAdd`, `$checkedAdd` and
friends on every integer, with the same three families in `std::math`.
`--safety none` never traps, and `@Config(overflow_checks == "on")` says
which world a declaration is in.

### 4. Nested associated-type projection

`I::Item` works. `I::Item::Item` does not:

```
ERROR: 'Counter' has no associated type 'Item' [E0247]
```

This is what blocks `Iterator::flatten`, which is why `std::iter` has no
`flatten` today: the adaptor's own `Item` is the item of its source's item,
and there is no way to write that down. The same gap will block any adaptor
whose output type is two marks deep.

**Verdict: do it.** It is a resolution change, not a design question, and
there is a concrete thing waiting on it.

**Done.** The projection itself already resolved; what did not was the
`where I::Item: Iterator` a conditional bind needs in order to apply only
where it holds — a clause whose subject is a projection was ignored, and a
resolved annotation was cached on its node and handed to every later
instantiation. Both fixed, and `flatten` and `flatMap` are in `std::iter`.

### 5. The two spellings of a function type

A function type has two forms: `@function(args...) -> ret`, and a leading form
`@function(ret, args...)` where the first element is the *result*. They are
told apart by whether an arrow follows.

That makes `@function(Self::Item)` legal and mean "takes nothing, returns a
`Self::Item`" — which is almost never what someone writing it intends. There
is no diagnostic, because there is nothing wrong with it; the error surfaces
later, at the call, as `'this function value' takes 0 argument(s) — 1 given`,
pointing at the call rather than at the annotation.

A one-element leading form is the only ambiguous case, and it is also the
least useful one. Warning on `@function(T)` with no arrow — "this declares a
function of no arguments returning `T`; write `@function() -> T` if that is
what you meant" — would cost nothing and remove the whole class of confusion.

**Verdict: warn.** Do not remove the leading form; it reads well with several
arguments.

**Done, further than the verdict.** The leading form is gone: `@function(args...)
-> ret` is the one spelling, and leaving the arrow off means the function
returns `()`, exactly as leaving `-> ret` off a `fn` does. `@function(T)` now
means what it looks like it means.

### 6. `std::iter` gaps that are only library work

`fold`, `reduce`, `forEach`, `last`, `nth`, `position`, `best`, `step_by` and
`inspect` landed with this revision. Still absent, and each is a few lines
once the projection above exists or a bound is decided:

* `flatten` / `flatMap` — **done**, with the projection above.
* `sum` / `product` — waiting on a way to name a type's zero and one. A
  `Numeric` mark with `zero()` and `one()` static requirements would do it,
  and `Self`-returning static requirements already work.
* `min` / `max` — the same, via `where Self::Item: operator::cmp`. `best`
  covers the case where a comparison is passed in.
* `peekable`, `windows`, `chunks`, `dedup`, `scan`, `unzip`, `partition`.
* `rev`, which needs a `DoubleEnded` mark with `nextBack`.

**Verdict: do them as the bounds arrive.**

### 7. `std::collections` gaps

`Vector`, `Map`, `Set`, `Buffer` and slices are there, and as of this
revision a vector can be read as a slice: `Vector::asSlice` views the
elements as a `[T]` without copying, over `mem::slice_of<T>(block, count)`,
the intrinsic that makes a slice from an address and a count for any
container to use. Missing, in the order they are likely to be wanted:

* **Sorting on `Vector`.** `slice::sortedBy` exists and allocates a new
  vector; an in-place `sort` and a `sortBy` on `Vector` itself is what
  everyone reaches for first.
* **A double-ended queue.** A ring buffer over the same allocator `Vector`
  uses; the one container whose absence is felt in ordinary code.
* **An ordered map.** `Map` is open-addressed and unordered; iteration order
  is an implementation detail. A B-tree map with ordered iteration is a
  different data structure, not a flag.
* **A small-size-optimised string builder.** `String + String` allocates;
  `io::Bytes` is the byte-level answer but is not a text API.
* **`BitSet`**, once there is a reason.

### 8. `std::text` and `std::io` gaps

`std::text` does normalisation, case folding and collation — the hard parts —
and, as of this revision, the everyday half too: `startsWith`, `endsWith`,
`contains`, `find`, `withoutPrefix`, `withoutSuffix`, `trim`, `split`,
`splitLines`, `replace`, `join`, `padStart`, `padEnd`.

What is still missing there is **case conversion for display** — `upper` and
`lower`. `foldCase` exists and is the right answer for comparison, but it is
not the right answer for showing a word to a reader: full case mapping is
locale-sensitive (Turkish dotless ı), context-sensitive (Greek final sigma)
and not one-to-one (ß to SS). It needs its own Unicode table, which is why it
is not there yet rather than an oversight.

`std::io` has files, byte buffers, streams and buffered reading, and as of
this revision directories too: `readDirectory`, `walkDirectory`, `isDirectory`
and `joinPath`, over four runtime calls that hide the difference between
`readdir` and `FindFirstFile`.

What is still missing is metadata (size, times, kind), rename, copy, temporary
files, and path manipulation beyond joining. A `std::path` module — splitting,
extension, parent, normalising — is the piece the rest waits on.

### 9. Things with no module at all

Ordinary programs want, and Rune has nothing for: **random numbers** (a
`std::random` with a seeded generator and an OS entropy source), **dates and
calendars** (`std::time` measures durations and reads a clock; it has no
notion of a date), **JSON**, **command line parsing**, **hashing beyond the
structural `mem::hash`**, and **environment variables** — which the runtime
could expose in a dozen lines.

**Done, all six.** `std::random` (xoshiro256** over SplitMix64, seeded from
a number or from the OS, plus `bytes` for secrets), a calendar in `std::time`
(`Date`, `TimeOfDay`, `DateTime` with a UTC offset, `utcNow`/`localNow`, ISO
8601 both ways, Hinnant's day arithmetic), `std::json` (the former example
package, promoted: a strict parser that says where it failed, a writer that
round-trips, `[]` for reaching in, `json!` for writing out), `std::cli` (a
declared parser with generated `--help`), `std::hash` (FNV-1a, CRC-32,
SHA-256, incremental or in one call) and `std::env`. The runtime side is a
few hundred lines of C: variables, entropy, the wall clock, and what the
local zone thinks the offset is.

The cost the previous section warned about is real and was paid attention
to: the modules add about five milliseconds of front-end work to every
compile. The code generator, which used to declare every library function
and lay out every library class up front, now does so only for what a
program reaches — which took a hello world's code generation from ten
milliseconds to under one and left the net cost of the new modules at about
five milliseconds on a fifty-millisecond compile.

---

## Medium term

Bigger. Each changes how programs are written, not just what they can call.

### 10. `impl Mark` in return position

`dyn Mark` exists and boxes. There is no way to return a concrete type without
naming it:

```
fn make() -> impl Shape { ... }
ERROR: Expected '{' — Got: 'Shape' [E0106]
```

This matters most for iterators. Every adaptor in `std::iter` returns a type
whose name spells out the whole chain — `Map<Filter<VectorIter<i64>>, String>`
— and a function that hands one back has to write it. Rust hit exactly this
wall and answered with `impl Trait`; Swift answered with `some Protocol`.

For Rune the implementation is an opaque type alias fixed at the definition
site: the compiler knows the concrete type, callers know only the mark, and
monomorphisation is unaffected. It costs nothing at run time, which is the
whole point of it over `dyn`.

**Verdict: worth it, and worth doing before the iterator library grows any
further**, because every adaptor added now is another type nobody can write.

**Done, as `some Mark`** — Swift's spelling, since `dyn Mark` is already the
other half of that pair. The body fixes the concrete type with the first
value it returns; every call yields that type; callers reach only what the
mark declares, and a caller that tries to name a field, cast, or take the
value apart is refused. Generics instantiate over it as a type of its own,
and it crosses a `.rul` boundary. What it does not do yet: appear anywhere
but a function's whole result (`Option<some Shape>` is refused), or be the
return type of a mark requirement, which has no body to decide it.

### 11. Const generics

A struct may be declared over a length:

```rune
struct Fixed<N, T> { pub items: [N:T] }      // parses
```

but there is no way to supply one — `Fixed<3, i64>` does not parse, because
the argument list expects types. `[N:T]` works as a *shape* in a structural
bind, so half the machinery is already there.

The full feature (C++'s non-type template parameters, Rust's const generics,
Zig's `comptime` parameters) is large, and its hard part is not parsing but
deciding how much arithmetic is allowed in a length. The small version —
integer parameters, no arithmetic beyond what constant folding already does —
would carry fixed-size matrices, ring buffers and inline vectors, and is
probably where to stop.

### 12. A stronger story for lifetimes

`--safety full` catches a borrow that outlives its binding, two borrows where
one can write, a copy out of an owning place, and a use after a move. That is
a real analysis, and it is intraprocedural. Nothing is written down at a
function boundary, so nothing is checked across one.

The options are the whole spectrum: named lifetimes on signatures (Rust, and
by far the largest cost in language surface), a single implicit region per
call (much weaker but nearly free), or leaving it where it is and treating
escaping borrows as the thing `Unique<T>` and reference counting exist to
avoid. Rune's position — safety as a dial, ARC as the default — makes the
third defensible in a way it is not for Rust.

**Verdict: not yet.** But the question should be answered deliberately rather
than by never asking it.

### 13. Concurrency beyond threads

`std::thread` has threads, mutexes, channels and atomics, and `Send`/`Sync`
reasoning. There is no asynchrony: no event loop, no `async`/`await`, no
non-blocking I/O. `std::net` blocks.

Three routes, in increasing order of what they cost the language:

1. **Non-blocking I/O with an explicit poller** — an `epoll`/`kqueue` wrapper
   in `std::io`, and programs written around it. No language change at all.
2. **Stackful coroutines** — a scheduler and a stack per task. A runtime, in
   the sense this language has been avoiding.
3. **`async`/`await` with compiler-generated state machines** — no runtime
   needed for the transform, but a large one for the executor, plus the
   colouring problem that follows every language that has adopted it.

The first is worth having regardless, and buys time to decide about the rest.

### 14. Procedural macros

`macro name { (pattern) => { expansion } }` is a token rewriter with
fragments, repetition and `stringify!`. It cannot inspect a type, derive an
implementation, or run arbitrary code.

The single most-wanted thing it cannot do is **derive**: `@derive(Display)`
on a struct, writing the obvious `bind`. That is worth having on its own, and
does not require general procedural macros — a fixed set of derivable marks,
implemented in the compiler, covers `Display`, equality, hashing and
comparison, which is most of what anyone derives.

**Verdict: do `@derive` for a closed set. Defer arbitrary compile-time code
execution**, which needs an interpreter for the language inside the compiler
and is a project of its own.

### 15. Overloading beyond `bind`

A `bind` may now supply one name several times, chosen by argument type. A
plain `fn` still may not:

```rune
fn parse(text: String) -> i64 { ... }
fn parse(text: CString) -> i64 { ... }   // not allowed
```

This is deliberate, and the reason is worth writing down: a `bind` is attached
to a type, so an overload set is bounded by that type and by the marks bound
to it, and a reader looking at `value.name(...)` knows where to look. A free
function's overload set is bounded by the module — and, with imports, by
everything the module can see.

The cost shows in the standard library. `std::process::panic` used to come in
two spellings because a `String` and a `CString` are different types; it is
now generic over `io::Display`, which is the language's real answer — a bound
covers what an overload would have, and says what it requires rather than
listing what it accepts.

**Verdict: leave free functions alone.** Where a genuine overload is wanted,
either the parameter has a bound in common (use a generic) or the two
functions do different things (give them different names).

---

## Longer term

### 16. Compile-time evaluation

`std::reflect` answers questions about types at compile time, and constant
expressions fold. There is no `const fn`, no compile-time loop, no way to
build a table at compile time. Zig's `comptime` is the maximal version of
this and reshapes the whole language around it; C++'s `constexpr` is the
incremental version and took four standards to become usable.

The Rune-shaped version is small: allow a `fn` marked `@const` to be evaluated
by the compiler when all its arguments are constants, restrict it to a subset
with no allocation and no foreign calls, and use it for array lengths and
global initialisers. Everything past that is a language inside the language.

### 17. A target story below libc

Cross compilation works: `rune targets` lists what a package configures, and
`[target.<name>]` in `Rune.toml` names a toolchain. What is not answered is
what happens with no operating system underneath: the runtime is C, it calls `malloc`, and `std::io` calls
`write`. A freestanding profile — no allocator unless one is supplied, no
`std::io`, `std::process::panic` routed to a hook — is what makes the language
usable for firmware, and is mostly a matter of deciding which parts of the
library are allowed to assume what.

WebAssembly is the same question with a different answer for each part.

### 18. Incremental compilation

Every build re-parses every dependency's source out of its `.rul`, because
that is how generics cross the boundary. That is fine at this size and will
not be at ten times it. The fix is a serialised, versioned representation of
a checked module — not source, not object code, but the type tables — and it
is the kind of thing that is much easier to add before there is a lot of code
depending on the current shape.

---

## Tooling

Ordered by how often their absence is felt.

* **A documentation site, rather than a page.** `rune doc` writes one
  `docs/target/index.html` holding the whole package, which is exactly right
  up to a point and wrong past it: a library the size of the standard library
  would be one enormous file. Splitting it needs a link scheme that survives
  the split, and is worth doing only once something is that big. *Since
  written:* the standard library has its own page now — `rune doc std::io`,
  built into the cache from the compiler's `--docs-stdlib` sidecar and one
  guide per module under `stdlib/docs/`, each with examples a script
  compiles — and at 932 entries it is still one file and still fine.
* **A language server.** Nothing else on this list changes the experience of
  writing Rune as much. The compiler already produces structured diagnostics
  with ranges and related locations, and a four-pass semantic analyser that
  can be asked about a name; the missing piece is the protocol and a way to
  answer without recompiling the world.
* **A formatter.** `rune fmt`. Cheap, and settles an argument nobody should
  be having. The parser keeps ranges, which is most of what it needs.
* **A package registry.** Dependencies are path-only today. A registry is a
  large amount of work that is not compiler work; a git-dependency source is
  most of the benefit for a fraction of it, and should come first. *Done, as
  static files:* a registry is an `index.toml` and a `packages/` tree of
  archives, served by `rune pkg server --serve` or by nothing at all; the
  client has `search`, `desc`, `add`, `remove`, `update`, `deps` and
  `installed`, packages install once under `~/.rune/pkg/<name>/<version>`
  with a list of the projects that use them, and `Rune.lock` pins a
  project's choices. Every archive a build needs is known from the index
  before the first is fetched, so they download at once. Registries have
  names — the one the index declares, or an alias given at `pkg server
  add` — kept globally, listed and removed with `pkg server list` /
  `remove`; `<registry>::<package>` and `--registry` name one wherever a
  package is named, a manifest records the choice as `registry = "..."`,
  and `rune doc <package>` opens an installed package's documentation,
  fetching it first if need be. What it does not have, and should before
  anyone runs one for strangers: a signed index, and yanking. A git source
  is still worth adding beside it.
* **A debugger story.** `-g` emits DWARF and the traceback demangles. What is
  missing is pretty-printers, so a debugger shows a `String` or a `Vector<T>`
  as its contents rather than as a pointer and two integers.
* **Sanitiser integration.** `RUNE_DEBUG_RC=1` poisons freed objects and
  reports reference-counting violations. Passing `-fsanitize=address` through
  to the C runtime and the generated code would catch the rest.
* **Benchmarking.** `rune test` exists; `rune bench` does not, and the
  language has no timer harness beyond `std::time`.

---

## Deliberately not planned

Each of these is a thing a language could have and Rune should not.

* **A garbage collector.** ARC with `weak` and `Unique<T>` is the memory
  model. A cycle collector would be the thin end of a runtime.
* **Exceptions.** `Result` plus `?` is the error model, and a second one that
  unwinds would double the surface of every API.
* **Implicit numeric narrowing.** Widening is implicit; narrowing needs `as`.
  This has been the right call every time it has come up.
* **Method-call syntax on free functions (UFCS).** It makes `value.name()`
  ambiguous with the method tables, which are the thing that makes marks
  legible.
* **Inheritance beyond single classes.** Marks with default methods cover
  what multiple inheritance is used for, without the diamond.
* **Truthiness.** `if 0` is an error and should stay one.

---

## How the list is meant to be used

The near-term entries are ordered so that each unblocks the next: error
conversion makes layered libraries writable, slice patterns make parsers
writable, nested projection unblocks the iterator library, and `impl Mark`
keeps that library's types from becoming unspeakable. Everything in the medium
list is a decision rather than a task, and should be made once, in writing,
before any of it is implemented.
