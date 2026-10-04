# When the package is built again

The built package is cached, and reused until one of the things it is made from changes:

| Rebuilt when | Not rebuilt when |
| --- | --- |
| a macro's code changes — any token of a `#macro fn`, a `#type(Macros)` file, or a lifted file's `std` imports | the code *around* a lifted macro changes, a comment changes, or a macro moves to another line |
| the standard library or the compiler changes | the program is built for another target |
| a macro is added, removed, or moved to another file |  |

> [!NOTE]
> **Where it lives**
>
> The cache lives in the system's temporary directory, under `rune-macros/`. Building a new version of a package removes the old one, a package nothing has used in a fortnight is cleared away, and a build that fails halfway leaves nothing behind that a later build could take for finished.
