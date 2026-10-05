# Ownership across the line

Memory Rust allocates is Rust's to free, and the crate has to export the function that does it — `free_text`, `polygon_free`. A Rune struct with a `deinit` holding the handle makes that automatic: the Rust object goes when the Rune value does.

```text
struct Shape {
    handle: *var geometry::Polygon

    fn deinit(&self) {
        unsafe { geometry::polygon_free(self.handle) }
    }
}
```

A Rust panic does not cross the boundary: an `extern "C"` function that panics aborts, which is what a Rune panic does too. `examples/rust_interop` puts all of this together and builds for Windows with `rune build --target windows` as well.

> [!NOTE]
> **Several crates**
>
> Each Rust static library carries a copy of Rust's standard library. Two crates link side by side on Apple's linker; a linker that reports duplicate Rust symbols wants them behind one crate instead — a small wrapper that depends on both and re-exports their C functions.
