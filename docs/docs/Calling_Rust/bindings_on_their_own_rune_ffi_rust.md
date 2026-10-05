# Bindings on their own: `rune ffi rust`

`rune ffi rust <crate>` writes the same module to standard output, or with `-o` to a file — to read before depending on a crate, or for one built some other way. `--target` says how wide `c_long` is. `rune doc ffi` has the details, and the way into a crates.io library that has no C ABI of its own: a small wrapper crate, with the library's types as opaque handles.

```sh
$ rune ffi rust rust/geometry -o src/geometry.rune
bound 13 functions from 'geometry'
```
