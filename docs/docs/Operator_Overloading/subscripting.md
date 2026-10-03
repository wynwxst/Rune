# Subscripting

**An index of any type you like**

```rune
import std::io

struct Grid {
    cells: [9:i64]
    width: i64
}

bind operator::index to Grid {
    fn index(&self, at: i64) -> i64 { self.cells[at] }
}

struct Lookup { keys: [3:String], values: [3:i64] }

bind operator::LeftSquareBracket to Lookup {
    fn index(&self, key: String) -> i64 {
        for i in 0..3 {
            if self.keys[i] == key { return self.values[i] }
        }
        -1
    }
}

fn main() -> i64 {
    let g = Grid { cells: [1, 2, 3, 4, 5, 6, 7, 8, 9], width: 3 }
    io::println(g[0].$str() + " " + g[4].$str() + " " + g[8].$str())

    let table = Lookup {
        keys: ["one", "two", "three"],
        values: [1, 2, 3],
    }
    io::println(table["two"].$str() + " " + table["missing"].$str())
    0
}
```

And more than one, on one type. `index` overloads the way every other bound method does — by what it takes — so a value can be reached both by position and by name, and `indexSet` follows the `index` the read chose.

**Indexed two ways**

```rune
import std::io

struct Row { pub id: i64, pub name: String }

bind operator::"[]" to Row {
    fn index(&self, at: i64) -> String {
        if at == 0 { self.id.$str() } else { self.name.$clone() }
    }
    fn indexSet(&var self, at: i64, value: String) {
        if at == 0 { self.id = value.$toInt() ?? 0 } else { self.name = value }
    }
}

// A second block for the same operator on the same type. What is between the
// brackets is what says which one runs.
bind operator::"[]" to Row {
    fn index(&self, field: String) -> String {
        if field == "id" { self.id.$str() } else { self.name.$clone() }
    }
    fn indexSet(&var self, field: String, value: String) {
        if field == "id" { self.id = value.$toInt() ?? 0 } else { self.name = value }
    }
}

fn main() -> i64 {
    var r = Row { id: 7, name: "ada" }
    io::println(r[0])          // by position
    io::println(r["name"])     // by name
    r["id"] = "9"
    r[1] = "grace"
    io::println(r[0] + " " + r[1])
    0
}
```
