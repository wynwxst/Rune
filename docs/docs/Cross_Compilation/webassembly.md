# WebAssembly

`--target wasm` builds a WebAssembly module that uses WASI for what a program needs from outside itself — standard streams, files, the clock, the environment, its arguments. The module is `<name>.wasm`, and anything that runs WASI runs it: wasmtime, wasmer, wasm3, a browser with a WASI shim, Node's `wasi` module.

It is built with the [WASI SDK](https://github.com/WebAssembly/wasi-sdk): clang, `wasm-ld` and wasi-libc in one directory. `rune` looks for it in `$WASI_SDK_PATH`, then where its installers put it — `/opt/wasi-sdk`, a versioned `/opt/wasi-sdk-*`, `~/.rune/toolchains/wasi-sdk`, `~/wasi-sdk` — and uses the `<triple>-clang` it ships, so nothing else has to be told the target.

```sh
$ export WASI_SDK_PATH=/opt/wasi-sdk-25.0-x86_64-linux
$ rune new hello && cd hello
$ rune run --target wasm
○ Preparing runtime for wasm32-wasip1
○ Compiling hello v0.1.0
○ Running target/wasm/debug/hello.wasm
Hello from hello!
$ wasmtime target/wasm/debug/hello.wasm
```

`rune run` and `rune test` start the module under the first of wasmtime, wasmer and wasm3 that is installed, with the current directory and the environment passed through — the two things a native program gets without asking, and a WASI one only when granted. A module run by hand gets only what its runner grants: `wasmtime --dir=. hello.wasm` to let it see the files here.

|  | `wasm` | `wasm-threads` |
| --- | --- | --- |
| Files, streams, clock, environment, arguments | yes | yes |
| `std::thread` | no: starting one panics | yes, with wasi-threads |
| `std::task` | a task that runs to the end without waiting; one that has to wait panics | yes |
| `std::net` | no: every call fails | no: every call fails |
| `std::process` commands | no: running one fails | no |
| Tracebacks | the runner's own | the runner's own |

*What WASI preview 1 provides, and so what a module can do.*

The differences are the platform's, not the compiler's. WASI preview 1 can use a socket it was handed but cannot make one, and has no processes. WebAssembly's stack is not memory a program can point at, so a task cannot be parked on one: under `wasm-threads` each task runs on a thread of its own, one at a time, and a switch is handing the processor from one to the next; under plain `wasm` there is only the one stack, and a task runs on it to the end.

> [!NOTE]
> **Why threads are a separate target**
>
> A threaded module imports its memory rather than defining it — every thread is an instance of its own, and they share that memory — and fewer runtimes accept one. `wasm-threads` is a target of its own for that reason; wasmtime runs it with `-W threads=y -S threads=y`.

Code that has to differ asks `@Config(family == "wasm")` or `@Config(os == "wasi")` — see **Conditional compilation** — and `std::arch` says `wasm32` with a 32-bit `usize`.
