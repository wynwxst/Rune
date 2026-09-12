# End-to-end tests: each `cases/*.rune` file is compiled, linked and run, and
# its stdout compared against the `// EXPECT:` lines in its header.
add_test(NAME rune_end_to_end
  COMMAND ${CMAKE_COMMAND}
    -DRUNEC=$<TARGET_FILE:runec>
    -DRUNTIME_LIB=$<TARGET_FILE:runeruntime>
    -DSTDLIB_DIR=${RUNE_STDLIB_DIR}
    -DCASE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cases
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/work
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
