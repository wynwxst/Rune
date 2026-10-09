# Where the time goes

`--time` reports each stage of a compile. The numbers are wall clock rather than CPU time — lexing, parsing and the ownership pass spread themselves over the cores they can see, and should read as the time they took rather than as the sum of what every thread spent. The header says how many threads were available so the two are not confused.

```sh
$ runec --time --check src/main.rune
  time report (8 threads available)
    read          0.0 ms    0.0 %
    lex           9.2 ms   15.3 %
    macros        0.8 ms    1.4 %
    parse         3.8 ms    6.3 %
    check        42.0 ms   77.0 %
    ----------------------------
    total        60.2 ms  100.0 %
```

`check` is usually the largest. A compilation reads the part of the standard library it can reach — what it imports and names, what the language itself leans on, and what those import in turn — in order to understand its own code; a compile that fails against that part is checked again against all of it, so the diagnostics are the same either way, and `--whole-stdlib` asks for all of it from the start. It is read, not emitted: what ends up in the artefact is only what the artefact reaches.
