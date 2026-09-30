# The build

`rune` reads `Rune.toml`, works out what has to be compiled, and calls `runec`.
It knows nothing about the language — it manipulates file paths, command lines
and a dependency graph.

Everything it needs from the compiler has to be expressible as a flag. That
constraint is what keeps `runec` usable on its own, and it is why there is no
"compiler server" or shared in-process state: a build is a set of `runec`
processes.

## Pages

- [The package manager](the_package_manager.md)
- [Incremental builds](incremental_builds.md)
- [Concurrency](concurrency.md)
- [Cross compilation](cross_compilation.md)
- [Bare metal](bare_metal.md)
- [Build scripts](build_scripts.md)
