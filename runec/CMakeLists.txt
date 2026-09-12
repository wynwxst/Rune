file(GLOB RUNEC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(runec_core STATIC ${RUNEC_SOURCES})

target_include_directories(runec_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(runec_core PUBLIC rune_llvm)

target_compile_definitions(runec_core PUBLIC
  RUNE_VERSION_STRING="${PROJECT_VERSION}"
  RUNE_DEFAULT_STDLIB_DIR="${RUNE_STDLIB_DIR}"
  RUNE_RUNTIME_LIB_DIR="${CMAKE_BINARY_DIR}/lib")

target_compile_options(runec_core PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations)

add_executable(runec tools/runec_main.cpp)
target_link_libraries(runec PRIVATE runec_core)
