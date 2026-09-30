# Where the output goes

A cross build gets a directory of its own, named after the target, so host and cross artefacts never overwrite each other and switching between them rebuilds nothing. A foreign target asked for by another spelling — `--target wasi` — still builds into its own name's directory.

```sh
target/debug/report                # the host
target/windows/debug/report.exe    # --target windows
target/wasm/debug/report.wasm      # --target wasm
target/pi/debug/report             # --target pi
```

> [!NOTE]
> **Executable suffix**
>
> The `.exe` is added for a Windows target, because a PE image is only executable with it, and `.wasm` for WebAssembly, because every runtime expects it.
