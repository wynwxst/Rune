# Build scripts

## The idea

`build.rune`, beside a package's `Rune.toml`, is a Rune program `rune` builds
and runs for the package: once before the package is compiled (**prepare**)
and once after each of its executables is linked (**finish**). It is Cargo's
`build.rs` with a second phase, because on bare metal the interesting step —
turning an ELF file into a disk image — comes after the link, and the only
other place for it was a shell script posing as a runner.

Both directions go through plain text so that the script is an ordinary
program: it is told where it is by `RUNE_*` environment variables and answers
with `rune:key=value` lines on its output. `stdlib/std/build.rune` wraps both
sides; nothing in the compiler knows about either.

## In `rune`

All of it is in `rune/src/main.cpp`, under *Build scripts*.

- `compileBuildScript` runs from `buildPackageLibrary`, the first step of every
  package, so everything the package compiles waits on it. It builds
  `target/build-script/build` with `runec` and no target flags at all — the
  script runs *here*, hosted, whatever the package is for — fingerprinted like
  any step, so it is recompiled only when `build.rune` or the toolchain
  changes.
- `runBuildScript` runs it with `pm::runCaptured`, from the package's
  directory, with the phase and the package's situation in the environment,
  and parses its output into a `ScriptAnswers`. A line is an answer only if it
  is `rune:` and a lower-case key before `=` — which keeps the runtime's own
  `rune: warning: ...` leak report from being read as one. Everything else is
  held back and shown on failure or under `-v`.
- The prepare phase's answers live on the `PackageNode`. `appendScriptConfig`
  adds its `--cfg`s to the library step and every target step;
  `appendScriptLink` its link answers to executables. Both are appended to
  the command line *before* its fingerprint is taken, so an answer that
  changes recompiles, and one that does not changes nothing.
- The finish phase runs at the end of `buildTarget`, for an executable, whether
  or not it was relinked; its `run` answer is carried out in the step's
  `Produced` slot (a slot per step, since steps run at the same time) into
  `BuildResult::RunWith`, which `rune run` looks the executable up in before
  starting it.

## Around it

Two smaller things came with it, both for running what a script makes:

- A runner may say where the program goes with `{}` (`launchCommand`), for an
  emulator that takes a file as an option's value rather than last.
- A runner word naming a file in the package is made absolute when the target
  is resolved (`anchorRunner` in `Targets.cpp`), and `rune` started below a
  package walks up to the nearest `Rune.toml`, so `rune run` works from
  anywhere inside one.

## Testing

`rune_build_script` (`tests/build_script_test.py`) makes a package whose
script sets flags with and without values, warns, and finishes by writing a
wrapper for `rune run` to start; it checks each, that a second build compiles
nothing, that an edited script is recompiled alone, and that a script's
failure stops the build with its message. `rune_bare_metal` boots TETRIS-OS
through `rune run` from its `src/` directory — the manifest found above, the
image made by its script, the runner's `{}`.
