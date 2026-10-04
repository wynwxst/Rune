# `#alias`: a second name

`#alias("other")` puts a declaration in scope under another name as well as its own, and more than one is allowed. The name is a string, so it can hold characters an identifier cannot — which is what lets an operator answer to its punctuation.

**One declaration, several names**

```rune
import std::io

struct Bag { count: i64 }

extend Bag {
    #alias("size")
    #alias("howMany")
    pub fn length(&self) -> i64 { self.count }
}

#alias("makeBag")
fn bag(n: i64) -> Bag { Bag { count: n } }

fn main() -> i64 {
    let b = makeBag(3)
    io::println(b.length().$str() + " " + b.size().$str() + " " +
                b.howMany().$str())
    0
}
```

> [!NOTE]
> **Same thing, other name**
>
> An alias is a name, not a copy: it reaches the same declaration, so there is one body to maintain and one symbol in the object file. Taking a name something else already holds is an error.
