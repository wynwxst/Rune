# Bindings borrow; `take` moves

A name in the pattern of a `match`, `if ... is` or `while ... is` borrows the part it matched, where it is: reading `dir` reads the element inside `words`, and `words` still has it afterwards. To move the part out instead — to return it, or store it — write `take` before the name. `` `name `` is the same thing, short; `rune fmt` spells it out.

**Borrowing, taking, and lending a part**

```rune
import std::io

enum Job { Named(String), Blank }

fn describe(j: &Job) -> String {
    match j {
        Job::Named(n) => "job " + n,        // borrowed
        Job::Blank => "no job",
    }
}

fn name(j: Job) -> String {
    match j {
        Job::Named(take n) => n,            // moved out: `j` is this function's
        Job::Blank => "",
    }
}

fn bump(slot: &var i64?) {
    if slot is Some(n) {
        let r = &var n                      // lends the payload itself
        *r += 1
    }
}

fn main() -> i64 {
    let j = Job::Named("build")
    io::println(describe(&j))
    io::println(name(j))
    var count: i64? = 41
    bump(&var count)
    io::println((count ?? 0).$str())
    0
}
```

`&name` and `&var name` lend the matched part itself, not a copy, so a function can hand out a borrow into what it was given — `Option::look` and `touch` are written that way. `&var` needs what was matched to be writable: a `&var` borrow, a `var` binding, or `&var self`. A `take` moves out of something this function owns; out of a borrow it is an error, as any move out of a borrow is, and it is not allowed in an arm with a guard, which runs before the arm is chosen. When what is matched is a temporary — `match parse(text)` — nothing else could reach it, so its parts are the bindings' own either way. `let` and `for` patterns take apart a value that is theirs, and move as they always have.

**Moving a borrowed binding**

```rune
enum Job { Named(String), Blank }

fn name(j: Job) -> String {
    match j {
        Job::Named(n) => n,
        Job::Blank => "",
    }
}

fn main() -> i64 { 0 }
```
