# Turning it on

`--memory zombie` on the command line, or `memory = "zombie"` under `[build]` in `Rune.toml`, compiles the whole program — and its standard library — under single ownership. `--memory arc` (the default) is reference counting, unchanged. A library is tagged with the mode it was built for, and mixing the two in one program is a hard error, because the object code of each assumes its own convention.

```sh
runec --memory zombie -o app app.rune
rune build            # with memory = "zombie" in Rune.toml
```

> [!NOTE]
> **A model, not a dial**
>
> Zombie is a whole second memory model, not a stricter setting of the first. Its findings are always errors — at every `--safety` level — because the generated code has no counts to fall back on: the checker's verdict is what makes it sound.
