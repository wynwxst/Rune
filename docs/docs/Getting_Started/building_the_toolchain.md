# Building the toolchain

The repository builds two binaries: `runec`, the compiler, and `rune`, the package manager. You need CMake 3.20 or newer, a C++20 compiler, and LLVM 17+ with development headers.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure

export PATH="$PWD/build/bin:$PATH"
```

If LLVM lives somewhere unusual, point CMake at it directly with `-DLLVM_DIR=$(llvm-config --cmakedir)`.

> [!NOTE]
> **Build the toolchain optimised**
>
> The build type matters more than usual, because `runec` links LLVM statically. A `Debug` build of the compiler is around 180 MB and spends about 20 ms getting to `main` before any work starts — which every compile in every build pays. `RelWithDebInfo` is the default when nothing says otherwise, and roughly halves the time a compile takes; use `Debug` only when stepping through the compiler itself.
