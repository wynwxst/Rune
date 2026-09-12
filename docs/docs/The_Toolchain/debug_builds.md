# Debug builds

`-g` emits DWARF — a compile unit, a subprogram per function with its source file and line, a type for every parameter and local, and a location on every instruction — so a debugger can break, step, and print variables by name.

```sh
$ runec -g -o list list.rune
$ lldb ./list
(lldb) b list.rune:24
(lldb) run
(lldb) frame variable
```

A `-g` build also keeps a frame pointer in every function and exports its symbols, which is what lets the runtime print a traceback when a program aborts. The frames are demangled back to the names you wrote.

**A traceback (this page is built with `-g`)**

```rune
import std::io

struct Grid { cells: [4:i64] }

extend Grid {
    pub fn at(&self, i: i64) -> i64 { self.cells[i] }
}

fn reach(g: Grid, depth: i64) -> i64 {
    if depth == 0 { return g.at(9) }
    reach(g, depth - 1)
}

fn main() -> i64 {
    let g = Grid { cells: [1, 2, 3, 4] }
    io::println("about to go out of range")
    reach(g, 2)
}
```

> [!NOTE]
> **Release builds stay quiet**
>
> Without `-g` the panic still names the file and line, but no traceback is printed: the frame pointer and the symbol names are not there to walk.
