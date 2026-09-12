# The guide

Four pages, in the order they are worth reading. The reference beneath them
is generated from the source and says what everything is; these say what to
do with it.

- [Using json](usage.md) — everything the library does, in the order you meet
  it: reading a document, looking inside one, building one, writing it back,
  and what happens when any of that fails.
- [The json! macro](macro.md) — writing a document out as JSON rather than
  building it a member at a time, and what the macro expands to.
- [Examples](examples.md) — whole programs, each one compiled and run.

Everything here is written by hand. `rune doc` finds it because it is in
`docs/`, and never writes over it: the generated half of the documentation
lives only in `docs/target/`.
