#!/usr/bin/env bash
# Builds Rune against a static LLVM (scripts/build-llvm.sh), tests it, and
# packs a release archive whose layout `rune` and `runec` find their files
# in wherever it is unpacked (runec/include/rune/Install.h):
#
#     rune-<version>-<platform>/bin/        rune, runec, rune-lint, ...
#                              /lib/rune/   the runtime library
#                              /share/rune/ the standard library, runtime
#                                           and tool sources, editor support
#
#     scripts/package.sh <llvm-prefix> <version> <platform> [--no-tests]
#
# As static as each platform allows: LLVM and the C++ runtime are linked
# in everywhere; on Windows the whole program is static; on Linux only the
# C library is left dynamic (glibc does not support anything else well),
# and the archive is built on an old glibc so it runs on newer ones.
set -euo pipefail

llvm="${1:?usage: package.sh <llvm-prefix> <version> <platform> [--no-tests]}"
version="${2:?}"
platform="${3:?}"
tests=1
[ "${4:-}" = "--no-tests" ] && tests=0

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-release-$platform"
name="rune-$version-$platform"
stage="$root/dist/$name"

linker=()
case "$(uname -s)" in
  Darwin)
    export MACOSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-12.0}"
    linker+=(-DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET")
    ;;
  Linux)
    linker+=(-DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc")
    ;;
  MINGW* | MSYS*)
    linker+=(-DCMAKE_EXE_LINKER_FLAGS="-static")
    ;;
esac

llvm_dir="$llvm/lib/cmake/llvm"
cmake -S "$root" -B "$build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_DIR="$llvm_dir" \
  -DRUNE_COPY_TOOLS_TO_HOME=OFF \
  -DCMAKE_INSTALL_PREFIX="$stage" \
  "${linker[@]}"
cmake --build "$build"

if [ "$tests" = 1 ]; then
  # The end-to-end cases are what say the compiler works on this platform.
  (cd "$build" && ctest --output-on-failure -R rune_end_to_end)
fi

rm -rf "$stage"
cmake --install "$build"
cp "$root/README.md" "$stage/" 2>/dev/null || true

# What the binaries still need from the machine, for the log: the C library
# and the system's own frameworks, and nothing else.
case "$(uname -s)" in
  Darwin) otool -L "$stage/bin/runec" "$stage/bin/rune" ;;
  Linux) ldd "$stage/bin/runec" "$stage/bin/rune" || true ;;
  MINGW* | MSYS*) ldd "$stage/bin/runec.exe" "$stage/bin/rune.exe" || true ;;
esac

# The installed toolchain works from where it was unpacked: build and run a
# program with nothing but the staged copy.
smoke="$(mktemp -d)"
(
  cd "$smoke"
  export RUNE_HOME="$smoke/home"
  # As a user would run it: not under the deployment target this build set.
  unset MACOSX_DEPLOYMENT_TARGET
  "$stage/bin/rune" new hello >/dev/null
  cd hello
  "$stage/bin/rune" run
)
rm -rf "$smoke"

mkdir -p "$root/dist"
cd "$root/dist"
case "$(uname -s)" in
  MINGW* | MSYS*) rm -f "$name.zip" && 7z a -tzip "$name.zip" "$name" >/dev/null ;;
  *) tar -czf "$name.tar.gz" "$name" ;;
esac
echo "packed $root/dist/$name"
