# End-to-end tests: each `cases/*.rune` file is compiled, linked and run, and
# its stdout compared against the `// EXPECT:` lines in its header.
# `LLVM_INCLUDE_DIRS` and `LLVM_LIBRARY_DIRS` may each name several
# directories; a case wants the one LLVM itself lives in.
list(GET LLVM_INCLUDE_DIRS 0 RUNE_LLVM_ONE_INCLUDE)
list(GET LLVM_LIBRARY_DIRS 0 RUNE_LLVM_ONE_LIBDIR)

# How a case links against that LLVM: `libLLVM` where it was built as one
# shared library, its static archives where it was not — and, when it was
# built with libc++ (as this compiler then is too), that C++ library rather
# than the platform's. `@LLVM_LINK@` in a case's `// FLAGS:` stands for it.
if(EXISTS "${RUNE_LLVM_ONE_LIBDIR}/libLLVM${CMAKE_SHARED_LIBRARY_SUFFIX}")
  set(RUNE_LLVM_CASE_LINK "-L ${RUNE_LLVM_ONE_LIBDIR} -l LLVM")
else()
  set(RUNE_LLVM_CASE_LINK "-L ${RUNE_LLVM_ONE_LIBDIR}")
  execute_process(
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/llvm-config" --link-static --libs core
    OUTPUT_VARIABLE RUNE_LLVM_STATIC OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(
    COMMAND "${LLVM_TOOLS_BINARY_DIR}/llvm-config" --link-static --system-libs
    OUTPUT_VARIABLE RUNE_LLVM_SYSTEM OUTPUT_STRIP_TRAILING_WHITESPACE)
  separate_arguments(RUNE_LLVM_STATIC UNIX_COMMAND "${RUNE_LLVM_STATIC} ${RUNE_LLVM_SYSTEM}")
  foreach(ARG ${RUNE_LLVM_STATIC})
    if(ARG MATCHES "^-l(.+)$")
      string(APPEND RUNE_LLVM_CASE_LINK " -l ${CMAKE_MATCH_1}")
    else()
      string(APPEND RUNE_LLVM_CASE_LINK " --link-arg ${ARG}")
    endif()
  endforeach()
  # Release builds of LLVM ship their archives as LTO bitcode, which only
  # LLVM's own linker reads; lld reads ordinary objects as well.
  find_program(RUNE_LLD NAMES ld.lld HINTS "${LLVM_TOOLS_BINARY_DIR}")
  if(RUNE_LLD AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    string(APPEND RUNE_LLVM_CASE_LINK
      " --cc ${CMAKE_C_COMPILER} --link-arg -fuse-ld=lld")
  endif()
endif()
if("${CMAKE_CXX_FLAGS}" MATCHES "-stdlib=libc\\+\\+")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -stdlib=libc++ -print-file-name=libc++.so
    OUTPUT_VARIABLE RUNE_LIBCXX OUTPUT_STRIP_TRAILING_WHITESPACE)
  get_filename_component(RUNE_LIBCXX_DIR "${RUNE_LIBCXX}" DIRECTORY)
  string(APPEND RUNE_LLVM_CASE_LINK " --cxx-stdlib libc++")
  if(IS_ABSOLUTE "${RUNE_LIBCXX_DIR}")
    string(APPEND RUNE_LLVM_CASE_LINK
      " -L ${RUNE_LIBCXX_DIR} --link-arg -Wl,-rpath,${RUNE_LIBCXX_DIR}")
  endif()
endif()

# A case may bring a C++ half (`// CXX: <path>`), compiled with the compiler
# this build uses; the LLVM the compiler was built against is what the C++
# interop cases call into, so its directories are passed along too.
add_test(NAME rune_end_to_end
  COMMAND ${CMAKE_COMMAND}
    -DRUNEC=$<TARGET_FILE:runec>
    -DRUNTIME_LIB=$<TARGET_FILE:runeruntime>
    -DSTDLIB_DIR=${RUNE_STDLIB_DIR}
    -DCASE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cases
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/work
    "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DLLVM_LIBDIR=${RUNE_LLVM_ONE_LIBDIR}"
    "-DLLVM_INCLUDE_DIR=${RUNE_LLVM_ONE_INCLUDE}"
    "-DLLVM_LINK=${RUNE_LLVM_CASE_LINK}"
    "-DLLVM_MAJOR=${LLVM_VERSION_MAJOR}"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/RunCases.cmake)

# The package registry, end to end: a registry made and served from files, a
# project that adds, builds against, updates and removes packages from it.
# Runs with its own RUNE_HOME, so the real cache is never touched.
find_program(RUNE_PYTHON3 python3)
if(RUNE_PYTHON3)
  add_test(NAME rune_registry
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/registry_test.py
            $<TARGET_FILE:rune>)
endif()

# The example ecosystem: seven packages with a diamond and two versions of
# two of them, a registry built from them, and two apps — every package's
# tests run against dependencies installed from that registry.
if(RUNE_PYTHON3)
  add_test(NAME rune_ecosystem
    COMMAND ${RUNE_PYTHON3} ${CMAKE_SOURCE_DIR}/examples/package/ecosystem.py
            check --rune $<TARGET_FILE:rune>)
endif()

# Cross targets: foreign targets and `[target]` tables resolved, and — with the
# WASI SDK and wasmtime installed — WebAssembly built and run, both flavours.
if(RUNE_PYTHON3)
  add_test(NAME rune_cross
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/cross_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# Build scripts: a package's build.rune, compiled for this machine and run
# before the package compiles and after each executable links.
if(RUNE_PYTHON3)
  add_test(NAME rune_build_script
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/build_script_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# Workspaces (`rune ws`): several packages under one Rune.toml, built in
# dependency order; and a package's local units, folders under src/ compiled
# on their own.
if(RUNE_PYTHON3)
  add_test(NAME rune_workspace
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/workspace_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# Bare metal: freestanding programs with no C library, every safety check
# reaching their own panic handler, and examples/toyos booted under QEMU.
# Each half is skipped where this machine cannot run it.
if(RUNE_PYTHON3)
  add_test(NAME rune_bare_metal
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/bare_metal_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# The editor tooling: `rune-lint` over files with known problems, and a whole
# language server session driven the way an editor drives it.
if(RUNE_PYTHON3)
  add_test(NAME rune_tooling
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/tooling_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# editors/vim, in Vim and Neovim run headless; an editor that is not
# installed is skipped.
if(RUNE_PYTHON3)
  add_test(NAME rune_vim
    COMMAND ${RUNE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/vim_test.py
            ${CMAKE_BINARY_DIR}/bin)
endif()

# The VS Code extension's grammar, with the TextMate engine VS Code uses.
# Needs `npm install` in editors/vscode first; skipped until then.
find_program(RUNE_NODE node)
if(RUNE_NODE AND EXISTS ${CMAKE_SOURCE_DIR}/editors/vscode/node_modules/vscode-textmate)
  add_test(NAME rune_vscode_grammar
    COMMAND ${RUNE_NODE} ${CMAKE_SOURCE_DIR}/editors/vscode/test/grammar.test.js)
endif()
