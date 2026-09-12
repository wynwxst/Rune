# Pointers, borrows and slices

Four kinds of indirection, in order of how much the compiler will do for you: shared borrows, mutable borrows, slices, and raw pointers.

| Written | Means | Checked | Needs `unsafe` |
| --- | --- | --- | --- |
| `&T` | shared borrow, read only | yes | no |
| `&var T` | mutable borrow | yes | no |
| `[T]` | slice: a borrow of a run of elements | yes | no |
| `*T` | raw pointer, read only | no | yes |
| `*var T` | raw mutable pointer | no | yes |

## Pages

- [Shared and mutable borrows](shared_and_mutable_borrows.md)
- [Slices](slices.md)
- [Raw pointers](raw_pointers.md)
