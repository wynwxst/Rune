cmake_minimum_required(VERSION 3.20)

# On macOS, LLVM's imported targets carry include paths that live inside the
# SDK — libedit's, for one. Handed to the compiler as bare `-isystem` entries
# with no sysroot in play, the platform headers stop being able to find each
# other and every build fails inside `<stddef.h>`. Naming the sysroot puts the
# machinery those headers expect back in place. This has to happen before
# `project()`, which is where CMake reads it.
if(APPLE AND NOT CMAKE_OSX_SYSROOT)
  execute_process(COMMAND xcrun --show-sdk-path
                  OUTPUT_VARIABLE RUNE_SDK_PATH
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(RUNE_SDK_PATH)
    set(CMAKE_OSX_SYSROOT "${RUNE_SDK_PATH}" CACHE PATH
        "macOS SDK the build compiles against" FORCE)
  endif()
endif()

project(Rune
  VERSION 0.1.0
  DESCRIPTION "The Rune programming language: compiler, runtime and toolchain"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# Everything lands in <build>/bin and <build>/lib so the `rune` driver can find
# its sibling tools with a single relative lookup.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---------------------------------------------------------------------------
# LLVM
# ---------------------------------------------------------------------------
if(NOT LLVM_DIR)
  # Homebrew keeps LLVM out of the default prefix to avoid clashing with the
  # Apple toolchain, so probe the usual spots before giving up.
  find_program(RUNE_LLVM_CONFIG NAMES llvm-config
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin /usr/lib/llvm/bin)
  if(RUNE_LLVM_CONFIG)
    execute_process(COMMAND ${RUNE_LLVM_CONFIG} --cmakedir
      OUTPUT_VARIABLE LLVM_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Rune: using LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")

llvm_map_components_to_libnames(RUNE_LLVM_LIBS
  core support irreader bitwriter analysis passes target
  ${LLVM_TARGETS_TO_BUILD})

# std::thread needs libpthread explicitly where glibc predates 2.34
# (the manylinux_2_28 release builds).
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(rune_llvm INTERFACE)

target_include_directories(rune_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(rune_llvm INTERFACE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(rune_llvm INTERFACE ${RUNE_LLVM_LIBS} Threads::Threads)
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(rune_llvm INTERFACE -fno-rtti)
endif()

# ---------------------------------------------------------------------------
# Where the toolchain looks for the runtime and standard library at run time.
# ---------------------------------------------------------------------------
set(RUNE_STDLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/stdlib" CACHE PATH
  "Directory containing the Rune standard library sources")

add_subdirectory(runec)
# The Rune half of the runtime needs the compiler, and the runtime library
# needs the object it produces, so the order is: compiler, Rune core, library.
add_subdirectory(runetime)
add_subdirectory(runtime)
add_subdirectory(rune)

enable_testing()
add_subdirectory(tests)

# The documentation generator is a Rune program, built with the compiler that
# was just built. `rune doc` looks for it beside `runec`.
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/bin/rune-doc
  # Written for reference counting; not yet ported to Zombie, the default.
  COMMAND $<TARGET_FILE:runec> --stdlib ${CMAKE_SOURCE_DIR}/stdlib
          --memory arc -o ${CMAKE_BINARY_DIR}/bin/rune-doc
          ${CMAKE_SOURCE_DIR}/tools/rune-doc.rune
  DEPENDS runec runeruntime ${CMAKE_SOURCE_DIR}/tools/rune-doc.rune
  COMMENT "Building rune-doc")
add_custom_target(rune_doc_tool ALL DEPENDS ${CMAKE_BINARY_DIR}/bin/rune-doc)

# The editor tools — the linter, the formatter and the language server — are Rune programs
# too, sharing the modules in tools/runetools/. `rune lint` and `rune lsp`
# look for them beside `runec`. The standard library is compiled into each,
# so a change to it rebuilds them.
file(GLOB RUNE_TOOLS_SHARED CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/runetools/*.rune)
file(GLOB_RECURSE RUNE_STDLIB_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/stdlib/std/*.rune)
foreach(tool rune-lint rune-fmt rune-lsp)
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/bin/${tool}
    # Written for reference counting; not yet ported to Zombie, the default.
    COMMAND $<TARGET_FILE:runec> --stdlib ${CMAKE_SOURCE_DIR}/stdlib
            --memory arc --module runetools -O2 -o ${CMAKE_BINARY_DIR}/bin/${tool}
            ${CMAKE_SOURCE_DIR}/tools/${tool}.rune ${RUNE_TOOLS_SHARED}
    DEPENDS runec runeruntime ${CMAKE_SOURCE_DIR}/tools/${tool}.rune
            ${RUNE_TOOLS_SHARED} ${RUNE_STDLIB_SOURCES}
    COMMENT "Building ${tool}")
endforeach()
add_custom_target(rune_editor_tools ALL
  DEPENDS ${CMAKE_BINARY_DIR}/bin/rune-lint ${CMAKE_BINARY_DIR}/bin/rune-fmt
          ${CMAKE_BINARY_DIR}/bin/rune-lsp)

# ---------------------------------------------------------------------------
# The tools in ~/.rune/bin
# ---------------------------------------------------------------------------
# `rune` runs its tools from ~/.rune/bin (or $RUNE_HOME/bin), copying them
# there from beside the compiler when it first needs one. A build does the
# same as soon as they are built, so an editor finds the newest straight
# away. Off for packaging and CI, where nothing should touch the home
# directory.
option(RUNE_COPY_TOOLS_TO_HOME "Copy the built tools into ~/.rune/bin" ON)
if(RUNE_COPY_TOOLS_TO_HOME)
  if(DEFINED ENV{RUNE_HOME})
    set(RUNE_TOOLS_HOME "$ENV{RUNE_HOME}/bin")
  elseif(DEFINED ENV{HOME})
    set(RUNE_TOOLS_HOME "$ENV{HOME}/.rune/bin")
  else()
    set(RUNE_TOOLS_HOME "$ENV{USERPROFILE}/.rune/bin")
  endif()
  set(RUNE_TOOL_BINARIES)
  foreach(tool rune-doc rune-lint rune-fmt rune-lsp)
    list(APPEND RUNE_TOOL_BINARIES ${CMAKE_BINARY_DIR}/bin/${tool}${CMAKE_EXECUTABLE_SUFFIX})
  endforeach()
  add_custom_target(rune_tools_home ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${RUNE_TOOLS_HOME}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNE_TOOL_BINARIES} "${RUNE_TOOLS_HOME}"
    DEPENDS rune_doc_tool rune_editor_tools
    COMMENT "Copying the tools to ${RUNE_TOOLS_HOME}")
endif()

# ---------------------------------------------------------------------------
# rune-ffi: built on first use, not here
# ---------------------------------------------------------------------------
# It needs libclang, which not every machine has, so the toolchain never
# builds it: `rune ffi` does, the first time it is run. Its tests run when
# this machine has a libclang to build it against.
find_library(RUNE_LIBCLANG NAMES clang libclang HINTS ${LLVM_LIBRARY_DIRS})
if(RUNE_LIBCLANG)
  get_filename_component(RUNE_LIBCLANG_DIR "${RUNE_LIBCLANG}" DIRECTORY)
  add_test(NAME rune_ffi
    COMMAND ${CMAKE_COMMAND}
      -DRUNEC=$<TARGET_FILE:runec>
      -DSTDLIB_DIR=${CMAKE_SOURCE_DIR}/stdlib
      -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
      -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/ffi
      -DLIBCLANG_DIR=${RUNE_LIBCLANG_DIR}
      -P ${CMAKE_SOURCE_DIR}/tests/RunFfi.cmake)
endif()

# ---------------------------------------------------------------------------
# Installing: the layout a release archive has
# ---------------------------------------------------------------------------
#   bin/              rune, runec and the tools
#   lib/rune/         the runtime library
#   share/rune/       the standard library, the runtime's sources (cross
#                     targets build theirs from them) and the tools' sources
# The binaries find all of it relative to themselves (runec/include/rune/
# Install.h), so the archive can be unpacked anywhere.
install(TARGETS runec rune RUNTIME DESTINATION bin)
install(PROGRAMS
  ${CMAKE_BINARY_DIR}/bin/rune-doc${CMAKE_EXECUTABLE_SUFFIX}
  ${CMAKE_BINARY_DIR}/bin/rune-lint${CMAKE_EXECUTABLE_SUFFIX}
  ${CMAKE_BINARY_DIR}/bin/rune-fmt${CMAKE_EXECUTABLE_SUFFIX}
  ${CMAKE_BINARY_DIR}/bin/rune-lsp${CMAKE_EXECUTABLE_SUFFIX}
  DESTINATION bin)
install(TARGETS runeruntime ARCHIVE DESTINATION lib/rune)
install(DIRECTORY stdlib/ DESTINATION share/rune/stdlib)
install(DIRECTORY runetime/ DESTINATION share/rune/runetime)
install(DIRECTORY runtime/src runtime/include DESTINATION share/rune/runtime)
install(DIRECTORY tools/ DESTINATION share/rune/tools
  PATTERN "target" EXCLUDE
  PATTERN "node_modules" EXCLUDE
  PATTERN "*.py" EXCLUDE)
install(DIRECTORY editors/ DESTINATION share/rune/editors
  PATTERN "node_modules" EXCLUDE)
