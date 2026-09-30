# How it works

`rune fmt` is a Rune program, `tools/rune-fmt.rune`; what it does to a file is
in `tools/runetools/format.rune`, which the language server shares.

## Asking the compiler

The types and labels come from the compiler itself:

```sh
runec --query-hints <file> <file>
```

checks the file — inside a package, with the package's other modules and the
libraries it was built against — and prints what could be written in, as JSON:

```json
{"hints":[{"offset":152,"kind":"type","label":": i64","insert":": i64"},
          {"offset":170,"kind":"parameter","label":"width:","insert":"width: "}]}
```

Each hint is a byte offset and the text to insert there; `insert` is empty when
the hint can be shown but not written, such as a `for` loop's element type. The
compiler records, for every argument given by position, which parameter it
landed in — the same matching that lets labels come in any order — and spells
each type through the file's own imports. A hint is kept only where the file's
text agrees: after the binding's name, or just inside a call's `(` or after a
`,`. That keeps anything the compiler made for itself — a macro's expansion, a
method call with its receiver moved into the argument list — from having text
written into the wrong place.

The language server asks the same question for its inlay hints.

## Checking the result

After the five steps, the result is checked with `runec --check`. If it does not
compile, the steps are run again without step 2; if that does not compile
either, the file is left as it was. Either way, `rune fmt` says what it held
back.

## Laying out

Steps 3 to 5 work on tokens, from the same lexer the linter and the language
server use, so a string or a comment is never mistaken for code. Whether a line
carries on a statement is the lexer's own decision — it ends statements exactly
where the compiler does.

Formatting a formatted file changes nothing.
