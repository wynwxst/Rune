# What does not cross

| Not supported | Instead |
| --- | --- |
| virtual dispatch | declare the member and call it on the exact type, or wrap the call in C++ |
| exceptions | a library that throws must not throw across the boundary; build it `-fno-exceptions`, or catch inside |
| `std::string`, `std::vector` and friends | pass their `data()` and `size()`, or wrap in C++ |
| multiple or virtual inheritance | single, non-virtual bases only — `this` has to be one address |
| MSVC targets | the Itanium ABI only: Clang, GCC, MinGW |
| a Rune `String`, closure or class | `CString`, `@cfunction`, or a C++ type declared in the block |

> [!WARNING]
> **Virtual functions**
>
> A member declared here is called **non-virtually**, by its own symbol. For a `virtual` function that is only right when the object's dynamic type is the one declaring it — which it is for a class you construct yourself, and is not for one handed to you through a base pointer. When in doubt, put a small non-virtual function in the C++ half and declare that.
