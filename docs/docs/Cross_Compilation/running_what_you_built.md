# Running what you built

A binary for another machine cannot simply be started. With a runner — the foreign target's, when one is installed, or the table's `runner` — `rune run` and `rune test` go through it; without one they build and say so, rather than reporting a test as passed when it never ran.

```sh
$ rune run --target windows        # wine is not installed
● built for x86_64-w64-mingw32, which this machine cannot run
  ─  note: install wine to run it here, or copy it to a machine that can
```
