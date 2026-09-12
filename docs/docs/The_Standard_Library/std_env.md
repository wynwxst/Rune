# std::env

The process's environment: its variables, and where it is. A variable that is not set is `nil`; one that is set but empty is an empty `String` — a shell script tells the two apart, so this does too.

| Function | Signature | Does |
| --- | --- | --- |
| `get` | `(name: String) -> String?` | the value, or `nil` when unset |
| `getOr` | `(name: String, fallback: String) -> String` | the value, or `fallback` when unset *or empty* |
| `has` | `(name: String) -> bool` | set at all, even to nothing |
| `set` | `(name: String, value: String) -> bool` | for this process and anything it starts |
| `remove` | `(name: String) -> bool` | unsets it |
| `all` | `() -> Vector<(String, String)>` | every variable, as `(name, value)` pairs |
| `currentDirectory` | `() -> String?` | the working directory, if it can be read |
| `setCurrentDirectory` | `(path: String) -> bool` | changes it |
| `home` | `() -> String?` | `HOME`, or `USERPROFILE` on Windows |
| `temporaryDirectory` | `() -> String` | `TMPDIR`, `TEMP` or `TMP`, else `/tmp` |

**Reading and writing the environment**

```rune
import std::io
import std::env

fn main() -> i64 {
    let editor = env::getOr("EDITOR", "vi")
    io::println(editor.$isEmpty())
    env::set("GREETING", "hello")
    io::println(env::get("GREETING") ?? "unset")
    env::remove("GREETING")
    io::println(env::get("GREETING") ?? "unset")
    io::println(env::has("PATH"))
    0
}
```
