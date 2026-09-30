# Tables in the image

A global whose initialiser is made only of constants — numbers, `bool`s, strings as `CString`, top-level functions as `@cfunction`, and arrays and structs of those — is emitted as data in the image, whether it is a `let` or a `global var`. Nothing runs to set it up, so it is ready before `rune_init`, and a font, a score table or an interrupt handler table costs no stack at start-up. Anything else is set by the generated initialiser, which is what `rune_init` runs.
