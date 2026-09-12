# std::env

The process's environment: its variables, and where it is. A variable that is
not set is `nil`; one that is set but empty is an empty `String` — a shell
script tells the two apart, so this does too.

## Reading

```rune
import std::io
import std::env

fn main() -> i64 {
    let editor = env::getOr("EDITOR", "vi")       // unset or empty → "vi"
    io::println(editor.$isEmpty())
    match env::get("RUNE_EXAMPLE_UNSET") {
        Some(v) => io::println("set to '" + v + "'"),
        None => io::println("not set"),
    }
    io::println(env::has("PATH"))
    io::println(env::home().hasValue())
    io::println(env::temporaryDirectory().$isEmpty())
    0
}
```

## Writing, and listing

```rune
import std::io
import std::env

fn main() -> i64 {
    env::set("GREETING", "hello")
    io::println(env::get("GREETING") ?? "unset")
    var count = 0
    for (name, value) in env::all() {
        if name == "GREETING" { count += 1 }
    }
    io::println(count)
    env::remove("GREETING")
    io::println(env::get("GREETING") ?? "unset")
    0
}
```

## Where the process is

```rune
import std::io
import std::env

fn main() -> i64 {
    let here = env::currentDirectory() ?? "?"
    io::println(here.$isEmpty())
    io::println(env::setCurrentDirectory("/definitely/not/a/directory"))
    0
}
```
