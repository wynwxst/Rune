# The two halves

`rune doc` builds one file, `docs/target/index.html`, and writes nothing else. What goes into it comes from two places that are independent of each other — either may be missing, and what is there is still built.

| Half | Where it comes from |
| --- | --- |
| **the reference** | every declaration marked `pub`, with the comment that was on it, read out of the compiler |
| **the guide** | every `.md` file under `docs/`, read exactly as it is written |

`docs/` is yours. There is no naming convention to follow, no front matter to add and no marker to write around: a file is a page because it is there. The generator only ever reads it.

```sh
docs/
  index.md                  where the reader starts
  guide/
    index.md                names the group its siblings sit under
    usage.md
    examples.md
  target/index.html         the built page — the only thing generated
```

> [!NOTE]
> **Either half on its own**
>
> A package with no `src/` still builds its guide, and a package with no guide still builds its reference. Documentation for something that is only prose is the same command.
