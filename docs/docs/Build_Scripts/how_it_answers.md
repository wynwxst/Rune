# How it answers

Each answer is a line on its output, `rune:key=value`; `std::build` writes them, and any program that prints them will do. Everything else it prints is shown when it fails, or under `-v`.

| `std::build` | Line | Does |
| --- | --- | --- |
| `cfg(name)` | `rune:cfg=name` | sets `@Config(name)` for the package's sources |
| `cfgValue(key, value)` | `rune:cfg=key=value` | sets `@Config(key == "value")` |
| `linkArg(arg)` | `rune:link-arg=arg` | added to every link of the package's executables |
| `linkLibrary(name)` | `rune:link-lib=name` | `-l<name>` |
| `linkPath(dir)` | `rune:link-path=dir` | `-L<dir>`, relative to the package |
| `runWith(path)` | `rune:run=path` | finishing: what `rune run` starts instead |
| `warning(text)` | `rune:warning=text` | shown; the build goes on |
| `fail(text)` | `rune:error=text` | the build stops, saying why |

A script that exits with anything but 0 fails the build, and its output is shown with the phase it failed in.

```sh
$ rune build
○ Compiling widgets v0.1.0 (build script)
● the build script of widgets failed in its prepare phase (exit 1): libwidget is not installed
```
