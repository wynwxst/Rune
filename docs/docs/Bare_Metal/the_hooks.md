# The hooks

Three functions the generated code relies on, supplied by the program. Each has a default in the freestanding runtime, marked `@weak`, that the program's own replaces.

| Attribute | Signature | Called for | Default |
| --- | --- | --- | --- |
| `@panicHandler` | `fn(message: CString, location: CString) -> Never` | every failed check, `unwrap` of an empty `Option`, `process::panic` | stops where it is, for ever |
| `@allocator` | `fn(size: usize, align: usize) -> *var u8` | every object a class, a closure or a `Unique` makes, and every block `std::mem` hands a container | panics: *this program allocates, and declares no @allocator* |
| `@deallocator` | `fn(block: *var u8)` | every object whose one owner is done with it | nothing — without an allocator nothing was allocated |

*A hook with the wrong signature is E0248; two of one kind is E0249.*

**The smallest allocator: a bump pointer that never frees**

```text
global var region: [65536:u8] = [0; 65536]
global var used: usize = 0

@allocator
fn allocate(size: usize, align: usize) -> *var u8 {
    let at = (used + align - 1) / align * align
    used = at + size
    unsafe { &var region[at as i64] as *var u8 }
}

@deallocator
fn release(block: *var u8) {}
```
