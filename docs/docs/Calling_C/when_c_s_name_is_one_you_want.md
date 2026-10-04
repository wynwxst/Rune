# When C's name is one you want

C has had the whole namespace for fifty years, so a good name is often already taken — `bind`, `connect`, `open`, `write`. `#as` renames the foreign declaration for Rune's side only: the symbol the linker resolves is still the one that was written, and the name is yours again.

**Importing a name you also want to declare**

```rune
import std::io

extern "C" {
    // Links against `strlen`; this file calls it `cLength`.
    #as("cLength")
    fn strlen(text: CString) -> u64
}

/// Which frees `strlen` up to mean something in Rune terms.
fn strlen(text: String) -> i64 { text.$charCount() }

#safe("strlen reads up to the NUL the String guarantees")
fn main() -> i64 {
    io::println(strlen("héllo"))                  // characters
    io::println(unsafe { cLength("héllo".$cstr()) })  // bytes
    0
}
```

> [!NOTE]
> **Note**
>
> `#as` applies to an `extern` declaration and nowhere else — on an ordinary function it would silently do nothing, which is worse than being told, so it is refused. `#alias` is the one that adds a second name to a declaration of your own.
