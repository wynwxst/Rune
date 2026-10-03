# Decorators you write yourself

Any name that is not one of the built-ins above is a decorator the program has to have declared: a function whose **last parameter is a function**. The earlier parameters are the decorator's own arguments, and the decorated function fills the last one.

**`@route(...)` is a call to `route(..., health)`**

```rune
import std::io

// The decorator: two arguments of its own, then the function it decorates.
fn route(method: String, path: String, handler: @function() -> ()) {
    io::println("registering " + method + " " + path)
}

@route("GET", "/health")
fn health() { io::println("200 healthy") }

@route("POST", "/reload")
fn reload() { io::println("202 reloading") }

fn main() -> i64 {
    io::println("--- main ---")
    health()
    0
}
```

Each decorator is called **once, before `main`**, after the globals exist and in the order the decorators were written. That is what makes a registry possible: whatever the decorators filled is ready by the time anything reads it.

**A registry filled before `main`**

```rune
import std::io
import std::collections::vector

// Made by the first decorator to run: decorators run before `main`, while
// globals are still being set, so the list cannot count on being there yet.
global var table: vector::Vector<String>? = nil

@safe("decorators run one at a time, before main, and nothing else holds the list")
fn route(path: String, handler: @function() -> ()) {
    if table.isNil() { table = vector::Vector<String>() }
    unsafe { table.touch().push(path) }
}

@route("/health")
fn health() { }

@route("/version")
fn version() { }

fn main() -> i64 {
    let routes = table.look()
    io::println(routes.length().$str() + " registered before main")
    for path in routes { io::println("  " + path) }
    0
}
```

| Rule | Why |
| --- | --- |
| the last parameter is a function | that is where the decorated function goes |
| the shapes have to agree | the decorator receives the function it is written on |
| the decorator cannot be generic | there is nothing to infer its arguments from before `main` |
| free functions only | a method carries `self`, and no instance exists yet |
| a built-in name wins | `@inline` and the rest are the compiler's, and cannot be redefined |

**An unknown decorator is an error, not a comment**

```rune
@memoise
fn slow(n: i64) -> i64 { n * 2 }

fn main() -> i64 { slow(21) }
```

**The decorated function has to be the shape asked for**

```rune
fn dec(label: String, f: @function() -> ()) { }

@dec("x")
fn f(v: i64) -> i64 { v }

fn main() -> i64 { 0 }
```

> [!NOTE]
> **Reading a function type**
>
> `@function(i64)` takes an `i64` and returns nothing, and `@function() -> i64` takes nothing and returns one: the parentheses always hold the parameters, and the arrow always introduces the result.
