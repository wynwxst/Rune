# Comments

```rune
// A line comment runs to the end of the line.

/* A block comment,
   which /* nests */ correctly. */

/// Three slashes is still just a line comment; it reads as documentation
/// but the compiler treats it the same.
pub fn documented() -> i64 { 1 }
```
