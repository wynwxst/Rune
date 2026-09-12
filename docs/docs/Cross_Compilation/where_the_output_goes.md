# Where the output goes

A cross build gets a directory of its own, so host and cross artefacts never overwrite each other and switching between them rebuilds nothing.

```sh
target/debug/report              # the host
target/mingw/debug/report.exe    # --target mingw
target/pi/debug/report           # --target pi
```

> [!NOTE]
> **Executable suffix**
>
> The `.exe` is added for a Windows target, because a PE image is only executable with it.
