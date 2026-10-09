file(GLOB RUNEC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(runec_core STATIC ${RUNEC_SOURCES})

target_include_directories(runec_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(runec_core PUBLIC rune_llvm)

target_compile_definitions(runec_core PUBLIC
  RUNE_VERSION_STRING="${PROJECT_VERSION}"
  RUNE_DEFAULT_STDLIB_DIR="${RUNE_STDLIB_DIR}"
  RUNE_RUNTIME_LIB_DIR="${CMAKE_BINARY_DIR}/lib"
  RUNE_RUNETIME_DIR="${CMAKE_SOURCE_DIR}/runetime")

target_compile_options(runec_core PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)

add_executable(runec tools/runec_main.cpp)
target_link_libraries(runec PRIVATE runec_core)

# Every LLVM target is linked in so one binary can cross-compile anywhere, but
# much of what LLVM's archives bring along is never reached — and on macOS the
# unused parts were enough to load a whole SMT solver's dylib at every start.
# Dropping them shrinks the binary and takes a quarter off start-up, which a
# compiler started once per file pays every time.
if(APPLE)
  target_link_options(runec PRIVATE LINKER:-dead_strip LINKER:-dead_strip_dylibs)
elseif(UNIX)
  target_link_options(runec PRIVATE LINKER:--gc-sections LINKER:--as-needed)
endif()
