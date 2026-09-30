# What it is told

| `std::build` | Environment | Is |
| --- | --- | --- |
| `preparing()` / `finishing()` | `RUNE_BUILD_PHASE` | `prepare` or `finish` |
| `packageName()`, `packageVersion()` | `RUNE_PACKAGE_NAME`, `_VERSION` | from the manifest |
| `packageDir()` | `RUNE_PACKAGE_DIR` | where `Rune.toml` is, absolute |
| `outDir()` | `RUNE_OUT_DIR` | `target/<target>/<profile>`, where outputs go |
| `profile()`, `release()` | `RUNE_PROFILE` | `debug` or `release` |
| `target()`, `triple()` | `RUNE_TARGET`, `_TRIPLE` | the `--target` name, or `host`; its triple |
| `freestanding()` | `RUNE_FREESTANDING` | built with no hosted runtime |
| `cc()` | `RUNE_CC` | the target's C compiler |
| `linker()`, `linkerKind()` | `RUNE_LINKER`, `_KIND` | what links, and whether it is `ld` or a `driver` |
| `artifact()`, `artifactName()` | `RUNE_ARTIFACT`, `_NAME` | finishing: the executable just linked |
