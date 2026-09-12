# When an expansion is wrong

The code a macro stands for is not in the file, so an error inside one would otherwise point at a call and complain about something the reader cannot see. Every expanded token carries the range of the invocation, and the diagnostic carries the expansion with it:

```sh
6 ║     io::println(sum!(1, "two", 3).$str())
                    ^^^ ERROR: cannot apply '+' to 'i64' and 'String'
    ─  note: in the expansion of `sum!(1, "two", 3)`
    ─  note:   which stands for: 0 + 1 + "two" + 3
```

A macro whose body calls another gives a chain, outermost first, so the note follows the same path the compiler did. A long chain keeps its two ends — the call as written, and the expansion that actually failed — and elides the middle.

> [!NOTE]
> **Small bodies read better when they break**
>
> This is why a macro is worth keeping small. The expansion is shown in the diagnostic, so a body that is a page of tokens produces a note that is a page of tokens.
