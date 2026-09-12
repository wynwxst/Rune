# Compile-time reflection

`std::reflect` asks the compiler what it already knows in order to lay a value out. Every answer is settled while compiling, so a call costs what its answer costs — usually nothing at all.

This is reflection in the sense Rust and Swift mean it — `size_of`, `type_name`, `offset_of!`, `MemoryLayout` — rather than the sense where a program discovers types it was not compiled against. There is no descriptor to carry and nothing to look up. `Any` is the runtime half, and answers a narrower question: what is *this value*?

## Pages

- [Asking about a type](asking_about_a_type.md)
- [It really is compile time](it_really_is_compile_time.md)
- [Reading a value](reading_a_value.md)
- [What is not here](what_is_not_here.md)
