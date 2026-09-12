# `pub`

**Public surface, private workings**

```rune
import std::io

// A module keeps its workings to itself and publishes an answer. Nothing
// without `pub` can be named from outside this file.
pub struct Account { pub owner: String, balance: i64 }

global var nextId: i64 = 1

fn nextAccountId() -> i64 {
    let id = nextId
    nextId += 1
    id
}

pub fn open(owner: String, deposit: i64) -> Account {
    let _ = nextAccountId()
    Account { owner: owner, balance: deposit }
}

pub fn balanceOf(a: Account) -> i64 { a.balance }

fn main() -> i64 {
    let a = open("ada", 250)
    io::println(a.owner + " has " + balanceOf(a).$str())
    0
}
```

Everything is private to its module unless marked `pub`. That applies to functions, types, fields, methods, globals and marks independently: a public struct with private fields is a perfectly ordinary thing to write.

**Visibility is per declaration**

```rune
/// Public type, mixed fields.
pub struct Reading {
    pub label: String       // callers may read and write this
    raw: i64                // module-private: an implementation detail
}

pub fn reading(label: String, raw: i64) -> Reading {
    Reading { label: label, raw: raw }
}

global scaleFactor: i64 = 100   // private to this module

pub fn scaled(r: Reading) -> f64 {
    (r.raw as f64) / (scaleFactor as f64)
}
```

| Marked | Visible to | Notes |
| --- | --- | --- |
| nothing | its own module | the default |
| `pub` | any module that imports it | recorded in the `.rul` |
| `pub` field | readers of the type | the type must be `pub` too |
| `pub` method | callers of the type |  |
| `pub mark` | anyone, to bind | requirements come with it |
