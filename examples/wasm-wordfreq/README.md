# wordfreq, in WebAssembly

The most common words in a text file — an ordinary Rune program, built for
WebAssembly and run under WASI.

```sh
rune run -- sample.txt          # the top 10
rune run -- sample.txt 3        # the top 3
```

```
sample.txt: 60 words, 20 different
  10	it
  10	of
  10	the
```

`[build] target = "wasm"` in `Rune.toml` makes WebAssembly the default here,
so `rune build` writes `target/wasm/debug/wordfreq.wasm` and `rune run` runs
it with wasmtime (or wasmer, or wasm3 — the first one installed).

Needs the [WASI SDK](https://github.com/WebAssembly/wasi-sdk/releases) —
unpacked anywhere, with `WASI_SDK_PATH` pointing at it, or in `/opt/wasi-sdk`
— and a WebAssembly runtime such as [wasmtime](https://wasmtime.dev). `rune
targets` says what it found.

## What WASI gives it

The module reads its arguments and the file through WASI, and sees only the
directories its runtime grants it. `rune run` grants the current one
(`wasmtime run --dir=.`), so `sample.txt` is readable; anything else is not:

```sh
$ wasmtime run --dir=. target/wasm/debug/wordfreq.wasm /etc/hostname
wordfreq: cannot read /etc/hostname
```

The `.wasm` file runs anywhere a WASI runtime does — Linux, macOS, Windows,
x86 or ARM — without being rebuilt.

## The same program, natively

Nothing in `src/main.rune` is specific to WebAssembly. Remove the `target`
line from `Rune.toml`, or compile it directly, and it is a native program:

```sh
runec src/main.rune -o wordfreq && ./wordfreq sample.txt 3
```

For real threads in WebAssembly — shared memory and `wasi-threads` — build
with `--target wasm-threads`; see *Cross compilation* in the reference.
