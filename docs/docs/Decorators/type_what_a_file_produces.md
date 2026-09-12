# `@type`: what a file produces

A file can say what it is meant to become. `@type` comes before every other directive, because what a file produces is what decides how the rest of them are used. A file with a `main` and no `@type` is an executable, which is what the compiler already assumed.

| Written | Produces |
| --- | --- |
| `@type(Executable)` | a linked program |
| `@type(Library)` | a `.rul` — object code plus the interface |
| `@type(Object)` | a `.o` and nothing else |
| `@type(Assembly)` | target assembly |
| `@type(LLVM)` | textual LLVM IR |

*A flag on the command line still wins: a build script has the last word over a file's preference.*

**A file that compiles to an object**

```text
@type(Object)

// No entry point is emitted for an object, so nothing runs global
// initialisers for it — anything constant has to be a function.
pub fn checksum(bytes: [u8]) -> u64 {
    var sum = 0 as u64
    for b in bytes { sum = sum * 31 + (b as u64) }
    sum
}
```

**`@type` comes first**

```rune
@link("m")
@type(Object)

fn f() -> i64 { 0 }
```
