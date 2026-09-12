# Measuring

## `--time`

```
$ runec --time --check src/main.rune

  time report (8 threads available)
    read          0.0 ms    0.0 %
    lex           9.2 ms   15.3 %
    macros        0.8 ms    1.4 %
    parse         3.8 ms    6.3 %
    check        42.0 ms   77.0 %
    ----------------------------
    total        60.2 ms  100.0 %
```

Wall clock, not CPU time: `lex`, `parse` and the ownership pass spread over
cores, and should read as the time they took rather than the sum of what every
thread spent. The header says how many threads were available so the two are
not confused. `RUNE_JOBS=1` gives the serial number for comparison.

The total does **not** include process start-up. Compare it against the wall
time of the whole command to see that: a debug build of `runec` spends about
20 ms getting to `main`.

## Profiling

The compiler is fast enough on small inputs that a sampling profiler will not
catch it. Generate something big first:

```sh
python3 - <<'PY' > /tmp/big.rune
for i in range(600):
    print(f"struct S{i} {{ pub a: i64 }}")
    print(f"fn use{i}() -> i64 {{ var v = vector::Vector<S{i}>(); v.push(S{i}{{a:{i}}}); v.length() }}")
PY
```

Then, on macOS:

```sh
runec --check /tmp/big.rune & sample $! 2 -mayDie -f /tmp/prof.txt
sed -n '/Sort by top of stack/,/Binary Images/p' /tmp/prof.txt | head -20
```

The "sort by top of stack" section is the one to read first. What you are
looking for is a *shape*, not a function name — if the top entries are
`__tree_next_iter` and `__tree_min`, something is walking a `std::map` in a
loop, and that is nearly always an accidental quadratic.

## Benchmarks worth keeping honest

| Measure | With |
| --- | --- |
| The whole suite | `ctest --test-dir build` — 121 end-to-end cases |
| A small compile | `runec --time` on a two-file program |
| A generic-heavy compile | `--check` on a generated file like the one above |
| A clean workspace build | `rune build -j1` versus `-j8` on a multi-package tree |
| A no-op rebuild | `rune build` twice; the second should be milliseconds |

The last one is the one that regresses silently. If a no-op rebuild starts
doing work, something has been added to a fingerprint that should not be in it.
