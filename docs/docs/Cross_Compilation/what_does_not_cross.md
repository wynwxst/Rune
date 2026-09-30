# What does not cross

The generated code is correct for every target LLVM supports. Three things are narrower than that:

|  | State |
| --- | --- |
| Struct by value across the C boundary | not on Windows x64 — rejected rather than misread; see **Calling C** |
| A panic message on Windows | the program aborts as it should, but the text does not reach standard error |
| `extern "C++"` | every Itanium-ABI target — Linux, macOS, the BSDs, MinGW; not `-windows-msvc` |
| Everything else in the FFI | portable: scalars, pointers, `CString`, `@cfunction`, `@export` |
