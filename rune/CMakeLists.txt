file(GLOB RUNE_PM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(rune ${RUNE_PM_SOURCES})

target_include_directories(rune PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/runec/include)

target_compile_definitions(rune PRIVATE
  RUNE_VERSION_STRING="${PROJECT_VERSION}"
  RUNE_DEFAULT_STDLIB_DIR="${RUNE_STDLIB_DIR}"
  RUNE_TOOLCHAIN_ROOT="${CMAKE_SOURCE_DIR}"
  RUNE_RUNTIME_LIB_DIR="${CMAKE_BINARY_DIR}/lib")

target_compile_options(rune PRIVATE -Wall -Wextra -Wno-unused-parameter)

# The registry client and server speak plain sockets; Windows keeps those in
# ws2_32, and everything else in libc.
if(WIN32)
  target_link_libraries(rune PRIVATE ws2_32)
endif()

target_link_libraries(rune PRIVATE Threads::Threads)

add_dependencies(rune runec runeruntime)
