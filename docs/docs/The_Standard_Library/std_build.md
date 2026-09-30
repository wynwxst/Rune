# std::build

For a package's `build.rune`: what it is told, and how it answers. See [Build scripts](#buildscripts).

| Function | Does |
| --- | --- |
| `preparing` / `finishing` | which phase this is: before the package compiles, or after an executable links |
| `packageName`, `packageVersion`, `packageDir`, `outDir` | the package, and where its outputs go |
| `profile`, `release`, `target`, `triple`, `freestanding`, `cc` | what it is being built as, and for |
| `artifact`, `artifactName` | while finishing: the executable just linked |
| `cfg`, `cfgValue` | set `@Config` flags for the package |
| `linkArg`, `linkLibrary`, `linkPath` | add to every link |
| `runWith` | while finishing: what `rune run` starts instead |
| `warning`, `fail` | say something; stop the build |
| `tool`, `run`, `runAll` | find the first of several programs on `PATH`; run one, failing the build unless it succeeds |
| `linking` | with `linker = "build-script"`: this call is the link |
| `linkArguments`, `linkInputs`, `linkOutput` | while linking: what a linker would have been given, the objects in it, and the file to write |
| `linker`, `linkerKind` | what links for the target, and whether it is a linker (`ld`) or a compiler (`driver`) |
