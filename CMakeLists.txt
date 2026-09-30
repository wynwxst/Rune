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

add_library(rune_llvm INTERFACE)

target_include_directories(rune_llvm SYSTEM INTERFACE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(rune_llvm INTERFACE ${LLVM_DEFINITIONS_LIST})
target_link_libraries(rune_llvm INTERFACE ${RUNE_LLVM_LIBS})
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
  COMMAND $<TARGET_FILE:runec> --stdlib ${CMAKE_SOURCE_DIR}/stdlib
          -o ${CMAKE_BINARY_DIR}/bin/rune-doc
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
    COMMAND $<TARGET_FILE:runec> --stdlib ${CMAKE_SOURCE_DIR}/stdlib
            --module runetools -O2 -o ${CMAKE_BINARY_DIR}/bin/${tool}
            ${CMAKE_SOURCE_DIR}/tools/${tool}.rune ${RUNE_TOOLS_SHARED}
    DEPENDS runec runeruntime ${CMAKE_SOURCE_DIR}/tools/${tool}.rune
            ${RUNE_TOOLS_SHARED} ${RUNE_STDLIB_SOURCES}
    COMMENT "Building ${tool}")
endforeach()
add_custom_target(rune_editor_tools ALL
  DEPENDS ${CMAKE_BINARY_DIR}/bin/rune-lint ${CMAKE_BINARY_DIR}/bin/rune-fmt
          ${CMAKE_BINARY_DIR}/bin/rune-lsp)
