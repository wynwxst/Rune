# Running what you built

A binary for another machine cannot simply be started. With a `runner`, `rune run` and `rune test` go through it; without one they build and say so, rather than reporting a test as passed when it never ran.

```sh
$ rune run --target mingw          # no `runner` configured
● built for x86_64-w64-mingw32, which this machine cannot run
  ─  note: copy the executable to the target machine, or give [target.mingw]
           a `runner` that can start it here (wine, qemu-aarch64, ...)
```
