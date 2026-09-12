# Writing a sequence

A container binds `Sequence` rather than `Iterator`, because a container is not a cursor over itself. It hands out a fresh one, so two loops over the same container — nested loops included — do not interfere.

**A container hands out a cursor**

```rune
import std::io

struct Grid { pub width: i64, pub height: i64 }
struct GridCells { pub grid: Grid, pub at: i64 }

bind Sequence to Grid {
    type Iter = GridCells
    fn iterate(&self) -> GridCells { GridCells { grid: *self, at: 0 } }
}

bind Iterator to GridCells {
    type Item = (i64, i64)
    fn next(&var self) -> Self::Item? {
        if self.at >= self.grid.width * self.grid.height { return nil }
        let cell = (self.at % self.grid.width, self.at / self.grid.width)
        self.at += 1
        cell
    }
}

fn main() -> i64 {
    let board = Grid { width: 3, height: 2 }
    // The binding is a pattern, so a cell arrives already taken apart.
    for (x, y) in board { io::print("(" + x.$str() + "," + y.$str() + ") ") }
    io::newline()
    for (x, y) in board { io::print((x + y).$str() + " ") }
    io::newline()
    0
}
```

> [!NOTE]
> **Note**
>
> The bind that satisfies `type Iter: Iterator` may be written after the one that needs it, as it is here. Declaration order does not decide whether the program compiles.
