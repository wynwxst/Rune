# Packages and `.rul` libraries

A library compiles to a single `.rul`: an object file plus the interface metadata for everything it marked `pub`. That is what replaces a header — importing from a dependency reads the metadata out of the `.rul`, so there is nothing to keep in sync.

```sh
$ runec --emit-lib -o libstatistics.rul src/lib.rune
$ runec -o report src/main.rune -I . -l statistics
```

> [!NOTE]
> **Usually you do not do this by hand**
>
> `rune build` does all of this for you, including building path dependencies first and passing their `link` entries down to whatever depends on them.
