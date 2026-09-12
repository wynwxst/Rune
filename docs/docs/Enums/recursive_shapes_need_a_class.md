# Recursive shapes need a class

An enum is a value, so a variant cannot contain the enum itself — that would have no finite size. Put the recursive part behind a class.

**A directly recursive enum**

```rune
enum List {
    Nil,
    Cons(i64, List),
}

fn main() -> i64 { 0 }
```

**The same shape, through a class**

```rune
import std::io

class Cell {
    pub head: i64
    pub tail: List
    fn init(self, head: i64, tail: List) {
        self.head = head
        self.tail = tail
    }
}

enum List {
    Nil,
    Cons(Cell),

    pub fn sum(&self) -> i64 {
        match self {
            List::Nil => 0,
            List::Cons(cell) => cell.head + cell.tail.sum(),
        }
    }
}

fn main() -> i64 {
    let list = List::Cons(Cell(1, List::Cons(Cell(2, List::Cons(Cell(3, List::Nil))))))
    io::println(list.sum())
    0
}
```
