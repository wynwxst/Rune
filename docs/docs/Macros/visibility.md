# Visibility

A macro is private to the file it is written in. `pub macro` puts it in scope everywhere — in every module of the package, and in every package that builds against it.

```sh
macro helper { ... }        // this file only
pub macro vec { ... }       // everywhere
```

Reaching another package works because a `.rul` carries its modules' source alongside their object code. That source is read back before anything is parsed, so the macros in it join the table like any other: a package ships a macro the way it ships a function.

The default matters more here than it does for a function. Expansion is one pass over the whole compilation, so without a default of private, every helper macro anyone wrote would be in scope in every file at once, and two packages could not both have a `debug!` without colliding.

| Written | In scope |
| --- | --- |
| `macro name { ... }` | the file it is written in |
| `pub macro name { ... }` | everywhere in the compilation |

> [!WARNING]
> **Macros are not namespaced**
>
> A macro is not reached through its module: `vec!` is written `vec!`, never `vector::vec!`. Expansion happens before imports mean anything, so a macro name is one flat namespace — which is exactly why the private default is worth keeping.

Two names collide when both are `pub`, or when both are in one file. Two private macros of the same name in different files never meet, so they are not a clash.

**A macro that is not there**

```rune
macro helper {
    ($x: expr) => { ($x) * 2 }
}

fn main() -> i64 {
    // A macro that is not in scope is reported as such, rather than the
    // grammar tripping over the `!` further along.
    missing!(1)
    0
}
```
