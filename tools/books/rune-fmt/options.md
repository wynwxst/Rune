# Options

```sh
rune-fmt [options] [path...]
```

| Flag | |
| --- | --- |
| `--check` | change nothing; print each file that is not formatted, and exit 1 if there is one |
| `--stdout` | print the one formatted file instead of writing it |
| `--no-types` | do not write binding types in |
| `--no-labels` | do not label arguments |
| `--no-reorder` | leave the order of the file's top-level pieces alone |
| `--no-lint-fixes` | do not apply the linter's style fixes |
| `--runec <path>` | the compiler; `rune fmt` passes its own |
| `--stdlib <dir>` | where the standard library is |

With no path, the package the current directory is in is formatted — its
`src/` and `tests/` — or, outside a package, every `.rune` file under the
current directory. A directory means every `.rune` file under it, build output
left out.

`--check` is for a build's continuous integration:

```sh
rune fmt --check || { echo "run rune fmt"; exit 1; }
```

The exit status is 0 when it succeeded, 1 with `--check` when something is not
formatted, and 2 when the command line or a file could not be read or written.

## In the language server

The server's settings — `rune doc lsp` — choose what **Format Document** writes
in, with the same switches: `format.types`, `format.labels`, `format.reorder`
and `format.lintFixes`, all on unless set to `false`. In VS Code they are
`rune.format.types` and so on.
