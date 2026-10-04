#!/usr/bin/env bash
# Builds the LLVM a release of Rune links against: static libraries only,
# the five backends Rune generates code for, and none of the optional
# dependencies (zlib, zstd, libxml2, terminfo, libedit, z3) — so the `runec`
# built against it needs nothing from the machine it runs on but the C
# library.
#
#     scripts/build-llvm.sh <version> <install-prefix>
#
# Runs on macOS, on Linux (a manylinux container in CI) and on Windows in an
# MSYS2 UCRT64 shell.
set -euo pipefail

version="${1:?usage: build-llvm.sh <version> <install-prefix>}"
prefix="${2:?usage: build-llvm.sh <version> <install-prefix>}"
work="${LLVM_WORK:-$PWD/llvm-work}"
mkdir -p "$work"
cd "$work"

archive="llvm-project-${version}.src.tar.xz"
if [ ! -d "llvm-project-${version}.src" ]; then
  curl -fsSL -o "$archive" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-${version}/${archive}"
  tar -xf "$archive"
fi

extra=()
case "$(uname -s)" in
  Darwin)
    extra+=(-DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}")
    ;;
esac

cmake -S "llvm-project-${version}.src/llvm" -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DLLVM_TARGETS_TO_BUILD="X86;AArch64;ARM;RISCV;WebAssembly" \
  -DLLVM_ENABLE_PROJECTS="" \
  -DBUILD_SHARED_LIBS=OFF \
  -DLLVM_BUILD_LLVM_DYLIB=OFF \
  -DLLVM_LINK_LLVM_DYLIB=OFF \
  -DLLVM_ENABLE_ZLIB=OFF \
  -DLLVM_ENABLE_ZSTD=OFF \
  -DLLVM_ENABLE_LIBXML2=OFF \
  -DLLVM_ENABLE_TERMINFO=OFF \
  -DLLVM_ENABLE_LIBEDIT=OFF \
  -DLLVM_ENABLE_LIBPFM=OFF \
  -DLLVM_ENABLE_Z3_SOLVER=OFF \
  -DLLVM_ENABLE_CURL=OFF \
  -DLLVM_ENABLE_HTTPLIB=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DLLVM_BUILD_TOOLS=OFF \
  -DLLVM_INCLUDE_UTILS=OFF \
  -DLLVM_ENABLE_BINDINGS=OFF \
  "${extra[@]}"
cmake --build build
cmake --install build
