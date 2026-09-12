# The C half. The memory core — allocation, reference counting, the weak
# table and raw memory — lives in `runetime/core.rune` and is archived in
# alongside it.
add_library(runeruntime STATIC src/rune_runtime.c src/rune_unicode_data.c ${RUNETIME_OBJECT})
set_source_files_properties(${RUNETIME_OBJECT} PROPERTIES
  EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_dependencies(runeruntime runetime_core)

target_include_directories(runeruntime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

set_target_properties(runeruntime PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  OUTPUT_NAME runeruntime)

target_compile_options(runeruntime PRIVATE -Wall -Wextra -Wno-unused-parameter)
