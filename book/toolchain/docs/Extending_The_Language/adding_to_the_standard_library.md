# Adding to the standard library

The standard library is Rune, under `stdlib/`. There is no registration step:
`Compilation.cpp` walks `stdlib/` recursively for `.rune` files and derives the
module name from the path.

```
stdlib/std/io.rune                  →  std::io
stdlib/std/collections/vector.rune  →  std::collections::vector
```

So adding `stdlib/std/text/casing.rune` gives you `std::text::casing`, and
nothing else has to be told.

## What that means for cost

Every file you add is lexed, parsed and type-checked by **every compile of
every program**, whether or not it is imported. It is not *emitted* unless
reached — see [What comes out](../The_Compiler/what_comes_out.md) — but the
front-end cost is unconditional.

That is the argument for keeping the standard library small and for putting
large optional things in packages instead. `check` is already the largest stage
of a compile, and it is mostly the standard library.

Measured when `env`, `random`, `hash`, `json` and `cli` went in — about
2,500 lines between them — the front-end cost was some five milliseconds on
a fifty-millisecond hello world. Two things keep it there, and are worth
copying in a new module:

- **no global that does work.** Globals are emitted and initialised in every
  program whether or not anything reaches them, so a table built at start-up
  or a generator seeded from the OS is paid for by every program. Initialise
  such a global to a value the code can recognise as "not yet" and build it
  on first use — `random::shared`, `hash::crcTable`;
- **a constant table is a constant.** `[0u32; 256]` and a written-out array
  of literals lower to one constant aggregate. Anything else lowers to a
  store per element, in `rune.init_globals`, for everyone.

The code generator's own half of the bargain is that it declares a library
function or lays out a library class only when something reaches it — see
[The invariants](../Performance/the_invariants.md).

## Calling the runtime

Anything needing the operating system is `extern "C"` on the Rune side and C in
`runtime/src/rune_runtime.c`:

```rune
extern "C" {
    fn rune_file_open(path: CString, mode: CString) -> *var u8
}
```

Adding one means editing both, and the C side has to build for every target the
toolchain supports — including Windows, where the sockets and threading code
already has its own branches.

## Things the compiler depends on by name

`Sema::findLangItems` looks these up by module and name. Renaming or moving one
breaks the language itself, silently, because the lookup just fails and the
feature stops working:

| What | Where it must be |
| --- | --- |
| `Option` | `std::option` |
| `Result` | `std::result` |
| `As` — what `into` dispatches through, and what `?` converts an error with | `std::convert` |
| `Iterator` — what `for` drives | `std::iter` |
| `Sequence` — what hands `for` an iterator | `std::iter` |
| `Send`, `Sync` | read off the type; never bound |

`Display` is the other one worth knowing: `io::println` takes anything bound to
it, and every builtin is.

## Marks, and why order does not matter

A `bind` in the standard library applies everywhere, and a `bind` in your
program applies inside the standard library. That is why the whole program is
checked at once, and why `DeferredBounds` exists — a bound on an associated
type may be answered by a `bind` written after the one that has to satisfy it.

You can therefore add a `bind io::Display to YourType` in a new file and
`println` picks it up, with no edit to `std::io`.

## Documenting it

`///` above a declaration is kept by the compiler (`Decl::Doc`) and written out
by `--emit-docs`, so `rune doc` picks it up with no further work. For the
language reference, add a sample to `docs/reference/content.py`: samples there
are compiled and run when the reference is built, so an example that stops
working becomes a build failure.

`rune doc std::<module>` opens the library's reference at the module. The
page is one file with a `#m-std-<module>` section per module, and the
address `rune` opens carries that fragment — through a one-line `jump.html`
beside the page, because `open`, `start` and `xdg-open` all take a
`file://` URL as a *file* and drop whatever follows the `#`. `--no-open`
prints the address instead, for a machine with no browser.
