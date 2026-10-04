# Starting without main

Under `#entry(none)` nothing runs before the program's own entry — which is usually a few lines of assembly that make a stack and call it. A global whose value is a constant or all zeros is in the image already. Anything else is set by `rune_init`, which the compiler generates and the entry calls first:

```text
extern "C" { fn rune_init() }

#export("kernel_main")
fn kernelMain(magic: u32, info: u32) -> Never {
    unsafe { rune_init() }
    // ...
}
```
