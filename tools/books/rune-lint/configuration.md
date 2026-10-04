# Configuring it

Every rule runs at one of three levels — `warn`, `note` or `allow` (off) — and
starts at its default, which `rune lint --list` shows. Four things can change
that, applied in this order, each over the one before:

1. the package's `[lint]` table in `Rune.toml`;
2. the command line, or the editor's settings;
3. an `#lint(...)` directive at the top of a file, for the whole file;
4. an `#lint(...)` directive before a declaration or statement, for just that
   item — and a directive inside another's item wins over the outer one.

## For a package: `Rune.toml`

A `[lint]` table is read by `rune lint` and by the language server alike, so
everyone who works on the package sees the same findings:

```toml
[lint]
allow = ["line-too-long", "naming"]
warn = ["missing-docs", "todo"]
max-line-length = 100
```

| Key | Meaning |
| --- | --- |
| `allow` | rules to turn off |
| `warn` | rules to turn on — including the ones that are off by default |
| `max-line-length` | the limit for `line-too-long`; `0` or absent means 120 |

`all` stands for every rule, so `allow = ["all"]` followed by a `warn` list is a
way of choosing exactly the rules you want. Turning on a `note` rule with `warn`
keeps it a note: the level says how serious a finding is, and a style rule does
not become a mistake by being asked for.

`rune build` and `rune check` ignore the table; it is only the linter's.

## For one run: the command line

```sh
rune lint --allow naming                 # or -A naming
rune lint --warn missing-docs            # or -W missing-docs
rune lint -A all -W unused-variable      # only this one
rune lint --max-line-length 100
```

A rule name that does not exist is refused, with exit status 2, rather than
quietly doing nothing.

## In the source: `#lint`

`#lint` is a directive, written like a decorator, that sets rules to a level:

```rune
#lint(allow(naming, line-too-long), warn(missing-docs), note(todo))
```

Each of `allow`, `warn` and `note` takes a list of rule names, and `all` stands
for every rule. A rule that does not exist, or a level that is not one of the
three, is itself reported, as [`invalid-lint`](rules.md#invalid-lint).

Where the directive sits decides what it covers.

### For a whole file

A directive is file-wide only when it is at the very top of the file — nothing
but comments and other directives before it — **and** nothing but other
directives and `import`s follow it before the first declaration:

```rune
// A module full of generated names.
#lint(allow(naming))

import std::io

fn Parse_Header() -> i64 { 0 }
```

### For one item

Anywhere else — including at the top of a file when a declaration comes
straight after it — the directive covers only the next item: a whole declaration
(its body included) or a single statement.

```rune
#lint(allow(naming))          // just this function, not the file
fn Parse_Header() -> i64 { 0 }

fn main() -> i64 {
    #lint(allow(unused-variable))
    let spare = 1

    #lint(allow(never-reassigned))
    var total = 0
    total
}
```

A directive on a class or `extend` covers every method in it; one on a method
inside it overrides that for the method alone. When a scope names a rule
outright and also says `all`, the rule's own entry wins.

A directive that has nothing after it to apply to — at the end of a block, say —
is reported as `invalid-lint` rather than silently ignored.

## In an editor

The language server reads the same `Rune.toml` table, and adds the editor's own
settings on top. In VS Code they are `rune.lint.enable`, `rune.lint.allow`,
`rune.lint.warn` and `rune.lint.maxLineLength`; `rune doc lsp` has the details,
and how other editors pass them.
