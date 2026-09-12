# Package layout

**Conventional structure**

```text
report/
├── Rune.toml           # the manifest
├── src/
│   ├── main.rune       # binary root  -> target/<profile>/report
│   └── lib.rune        # library root -> target/<profile>/report.rul
├── tests/
│   └── basics.rune     # one test program per file
└── target/
    ├── debug/
    └── release/
```

> [!NOTE]
> **Both at once**
>
> A package may have both roots. `src/main.rune` becomes the executable, `src/lib.rune` the library other packages import.
