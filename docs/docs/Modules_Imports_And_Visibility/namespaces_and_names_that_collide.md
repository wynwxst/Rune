# Namespaces, and names that collide

A package's name is the root of its namespace. `src/lib.rune` or `src/main.rune` **is** the package module, and every other file under `src/` is a module beneath it: `src/io.rune` in a package called `app` is the module `app::io`. Nothing is flattened, so two packages can both have a `shapes.rune` without ever meeting.

That means a package may freely declare a module whose short name is already taken by the standard library. `app::io` and `std::io` are different modules with different full names, and both are usable in the same file:

**Two `io` modules, one file**

```text
// A package named `app` with its own `src/io.rune`, alongside `std::io`.
// Both are reachable at once, because a qualified path is never ambiguous.

fn main() -> i64 {
    std::io::println("from the standard library")
    std::io::println(app::io::render("from this package"))
    0
}
```

Three rules decide what a name means, and they are worth knowing in this order:

| Rule | What it means |
| --- | --- |
| A **fully qualified path always works** | `std::io::println(...)` and `app::io::render(...)` name exactly one thing each, with no import at all. When in doubt, or when two short names would collide, write the path out |
| An **import binds the last segment** | `import std::io` puts `io` in scope; `import app::io` puts `io` in scope too. The name bound is the short one, not the path |
| The **last import wins** | importing both leaves `io` meaning whichever came second. It is not an error, and nothing warns — the earlier one is simply shadowed, and is still reachable by its full path |

> [!WARNING]
> **Where this bites**
>
> Because the last import wins silently, a file that imports two modules with the same short name should qualify both rather than rely on order. Reordering imports would otherwise change what the code means.

A submodule is a module in its own right, not an item inside its parent. `json::io` resolves even though `json` declares nothing called `io`, and it resolves the same way whether `json` is a package in this build or a `.rul` on the search path:

```sh
import json          // the package module
import json::io      // a module beneath it — a separate import

json::io::readLines(path)      // or fully qualified, with neither import
```

> [!NOTE]
> **How a binary imports its own library**
>
> A binary compiles under `<package>__bin_<file>` rather than under the package's own name, which is what lets `src/main.rune` say `import <package>` and reach the package's library instead of finding itself.
