# Running it

| Command | Does |
| --- | --- |
| `rune doc` | builds, reads every output root, writes `docs/target/index.html` |
| `rune doc --release` | the same, from an optimised build |
| `rune doc -v` | shows each compiler invocation |

Each output root is read separately, with the package’s components alongside it, exactly as the build compiles it — a binary that imports the package’s own library is read the same way it is built. The records are then merged, so one page describes the whole package however many targets it produces.

> [!NOTE]
> **It is written in Rune**
>
> The generator is `tools/rune-doc.rune` — a Rune program, using `std::io`’s directory listing to find the guide. The toolchain documents itself with itself, and the sidecar it reads is one `key value` line per field, so a replacement generator needs a line loop and nothing else.
