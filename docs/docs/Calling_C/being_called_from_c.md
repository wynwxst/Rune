# Being called from C

Go the other way with `@export`, which fixes the symbol name. The signature has to stay inside the shared vocabulary above — no classes, no `String`.

**C-callable entry points**

```rune
@export("stats_mean")
pub fn mean(values: [f64]) -> f64 {
    if values.$isEmpty() { return 0.0 }
    var total = 0.0
    for v in values { total += v }
    total / (values.$length() as f64)
}

@export("stats_scale")
pub fn scale(value: f64, factor: f64) -> f64 { value * factor }
```

A C program that calls those links the Rune object **and** the runtime, because the exported code is ordinary Rune code and may allocate, retain or release like any other.

```sh
$ runec -c -o stats.o src/lib.rune
$ cc host.c stats.o -lruneruntime -lm -o host
```

> [!NOTE]
> **Exported names are unique**
>
> `@export` gives a symbol strong linkage, so two of the same name collide rather than silently merging — which is what you want from something whose whole purpose is to answer to one exact name.
