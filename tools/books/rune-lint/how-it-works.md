# How it works

The linter is a Rune program, `tools/rune-lint.rune`, built on four modules it
shares with the language server:

| Module | What it does |
| --- | --- |
| `runetools::syntax` | lexes a file exactly as the compiler does — the same tokens, and the same newlines ending the same statements — but never stops at an error |
| `runetools::index` | reads the tokens for declarations, imports, and the names each function binds and where each is visible |
| `runetools::project` | finds what imports lead to — the standard library, the package's own modules, its path dependencies — and reads `Rune.toml` |
| `runetools::lint` | the rules |

`rune lint` runs the `rune-lint` built beside the compiler, or builds it from
`tools/` into `~/.rune/bin` the first time and again whenever those sources
change.

## What it knows, and what it does not

It reads the source, not the compiled program. It knows:

- every token, comment and line, and which statement each token is in;
- every declaration in the file, its signature, its documentation and whether it
  is `pub`;
- every name a function binds, and which binding each later use of a name refers
  to — including shadowing, patterns, loop variables and closures;
- where each import leads, and what that module declares.

It does not know types in general, does not run the borrow checker, and does not
expand macros. So each rule is written to be sure of what it says and to say
nothing when it cannot be:

- `never-reassigned` treats any method call on a variable as a possible change,
  since it cannot see whether the method takes `&var self`;
- `unused-import` leaves alone any module that could matter by being imported;
- `dead-code` only looks at private declarations, because only those are
  certainly not used by another file;
- `naming` never renames anything but a local, whose every use it can see.

When a finding and the compiler disagree about a program, the compiler is right —
and a finding that is wrong is a bug worth reporting, with the smallest file that
shows it.

## Where the rules live

Each rule is a function in `tools/runetools/lint.rune` that walks the tokens or
the bindings and reports a `Finding`: a rule name, a message, a byte range, and
optionally the edits that fix it. Adding one is adding a function, a line to
`rules()`, and a section to [Rules](rules.md) with an example that triggers it —
the toolchain's tests lint that example and fail if it does not.
