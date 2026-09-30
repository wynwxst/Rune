# What is in a `.rul`

A small container, and no more than it has to be. Every integer is little-endian, every string is a `u32` length followed by that many bytes of UTF-8, and there is no alignment or padding anywhere.

```ebnf
magic       8 bytes  "RUNELIB\1"
version     u32      the container's own version; 3 today
memory      u32      0 = reference counting, 1 = Zombie
name        string   the library's module name

flagCount   u32      `@Config` names set when this was built
  flag      string
valueCount  u32      `@Config` keys that had values
  key       string
  value     string

unitCount   u32      one per module compiled into the library
  path      string   the dotted module path, e.g. "geometry::shapes"
  source    string   its public interface, as Rune source

objectLen   u64
object      bytes    a native object file for one target
```

Three things about it are worth knowing.

|  | Why |
| --- | --- |
| The interface is **source**, not a symbol table | an importer re-parses it, so it gets the declarations exactly as they were written — including generic bodies, which monomorphisation needs, and `pub macro` definitions, which expansion needs. Everything not `pub` is stripped on the way in |
| The **conditions** travel with it | the interface is source, so its `@Config` conditions are answered again on import — and have to be answered the way they were when the object code was made, not the way the importer's own build would answer them |
| One target, one memory model | the object code bakes in retains and releases, or their absence and the moved-in argument convention. Importing a library built the other way is refused rather than linked, and there is no fat `.rul`: cross-compiling means building the dependency for that target too |

> [!NOTE]
> **No forward compatibility**
>
> The version is checked exactly rather than for a range. A mismatch says "built by a different compiler version" and stops, because the format is small enough that rebuilding is always the right answer.

What a library may export is everything a module may declare: types, generic types, enums, classes and their subclasses, marks with associated types and defaults, binds (including operators and `into` conversions), `extend` blocks, functions, globals, aliases and `pub macro`s. And what an importer may do with them is everything it could do with its own: name them, construct them, match them, **extend** them with methods of its own, and **bind** its own marks to them.
