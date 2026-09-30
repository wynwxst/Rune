Freestanding programs for `tests/bare_metal_test.py`: each case is compiled
with `sys.rune` (or `mini.rune`, which has no allocator) as a Linux x86_64
program built `@runtime(none)`, with no C library, and run on the host.
