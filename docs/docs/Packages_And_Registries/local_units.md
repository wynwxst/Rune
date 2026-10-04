# Local units

Inside one package, each folder directly under `src/` is a unit: a part compiled on its own, into a library of its own, before the package's files. Its files are its modules, named after the folder — `src/net/http.rune` is `net::http` — and a file named after the folder, `src/net/net.rune`, is `net` itself. Units import one another the same way, and are compiled in the order their imports need; a change to one recompiles it and the units and files that import it, nothing else.

```sh
src/
  main.rune            import unit::hello
                       import unit2::bye
  unit/hello.rune      pub fn hello() -> String { "hello" }
  unit2/bye.rune       import unit::hello

$ rune build
○ Compiling app::unit (unit)
○ Compiling app::unit2 (unit)
○ Compiling app v0.1.0
```

> [!NOTE]
> **Rules**
>
> A unit's name is part of the program's module namespace, as a package's is, so it cannot be the package's own name or that of a dependency. Units that import one another in a circle are an error naming one of them: a unit is compiled before those that import it. A package that depends on a library with units is handed the units too. Units belong to one package; to share code between packages, make it a package of its own in a workspace.
