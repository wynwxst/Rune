Freestanding programs for `tests/bare_metal_test.py`: each case is compiled
with `sys.rune` (or `mini.rune`, which has no allocator) as a Linux x86_64
program built `@runtime(none)`, with no C library, and run on the host.

`minimal.rune` and `minimal_float.rune` are compiled to objects only, on any
host, with `--cfg freestanding_type=minimal`: the first must leave the
runtime's float and hashing code out and come out smaller than with the full
runtime; the second, which turns a float into text, must be refused with
E0542 saying the minimal runtime leaves that out.
