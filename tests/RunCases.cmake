# Compiles, links and runs every tests/cases/*.rune, comparing what it prints
# against the expectations written in its header:
#
#   // EXPECT: <line of stdout>      one per expected line, in order
#   // EXPECT-PANIC: <substring>     the program must abort, stderr must match
#   // EXPECT-ERROR: <substring>     compilation must fail with this message
#   // LIB: <module> <path>          build <path> into <module>.rul first
#   // FLAGS: <args...>              extra `runec` flags for this case
#
# A `// LIB:` line names a Rune library the case imports. Its source is a path
# under `cases/`, and lives in `cases/lib/` so that the glob above — which only
# looks at the top level — does not pick it up as a case of its own. Each is
# compiled with `--emit-lib` into a directory of the case's own, which is then
# handed to the case as `-I`. Several may be given, and are built in order, so
# one library may import another.
#
# Invoked by CTest with RUNEC, RUNTIME_LIB, STDLIB_DIR, CASE_DIR and WORK_DIR.

cmake_minimum_required(VERSION 3.20)

file(GLOB CASES "${CASE_DIR}/*.rune")
list(SORT CASES)
file(MAKE_DIRECTORY "${WORK_DIR}")

set(FAILED 0)
set(PASSED 0)
set(FAILURES "")

foreach(CASE ${CASES})
  get_filename_component(NAME "${CASE}" NAME_WE)
  file(READ "${CASE}" TEXT)

  # Collect the expectations.
  set(EXPECT_LINES "")
  set(EXPECT_PANIC "")
  set(EXPECT_ERROR "")
  set(CASE_SAFETY "")
  set(CASE_LIBS "")
  set(CASE_FLAGS "")
  string(REPLACE "\n" ";" LINES "${TEXT}")
  foreach(LINE ${LINES})
    if(LINE MATCHES "^// EXPECT: ?(.*)$")
      list(APPEND EXPECT_LINES "${CMAKE_MATCH_1}")
    elseif(LINE MATCHES "^// EXPECT-PANIC: ?(.*)$")
      set(EXPECT_PANIC "${CMAKE_MATCH_1}")
    elseif(LINE MATCHES "^// EXPECT-ERROR: ?(.*)$")
      set(EXPECT_ERROR "${CMAKE_MATCH_1}")
    elseif(LINE MATCHES "^// SAFETY: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" CASE_SAFETY)
    elseif(LINE MATCHES "^// LIB: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" ONE_LIB)
      list(APPEND CASE_LIBS "${ONE_LIB}")
    elseif(LINE MATCHES "^// FLAGS: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" ONE_FLAGS)
      separate_arguments(ONE_FLAGS)
      list(APPEND CASE_FLAGS ${ONE_FLAGS})
    endif()
  endforeach()

  set(SAFETY_ARGS "")
  if(NOT CASE_SAFETY STREQUAL "")
    set(SAFETY_ARGS --safety "${CASE_SAFETY}")
  endif()
  list(APPEND SAFETY_ARGS ${CASE_FLAGS})

  # --- the libraries this case imports, built before it -------------------
  set(IMPORT_ARGS "")
  set(LIB_FAILED "")
  if(NOT CASE_LIBS STREQUAL "")
    set(LIB_DIR "${WORK_DIR}/${NAME}.libs")
    file(REMOVE_RECURSE "${LIB_DIR}")
    file(MAKE_DIRECTORY "${LIB_DIR}")
    set(IMPORT_ARGS -I "${LIB_DIR}")
    foreach(SPEC ${CASE_LIBS})
      string(REGEX MATCH "^([^ \t]+)[ \t]+(.+)$" _m "${SPEC}")
      if(NOT _m)
        set(LIB_FAILED "malformed `// LIB:` line: ${SPEC}")
        break()
      endif()
      set(LIB_MODULE "${CMAKE_MATCH_1}")
      set(LIB_SOURCE "${CASE_DIR}/${CMAKE_MATCH_2}")
      execute_process(
        COMMAND "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
                ${IMPORT_ARGS} --emit-lib --module "${LIB_MODULE}"
                -o "${LIB_DIR}/${LIB_MODULE}.rul" "${LIB_SOURCE}"
        RESULT_VARIABLE LIB_RC
        OUTPUT_VARIABLE LIB_OUT
        ERROR_VARIABLE LIB_ERR)
      if(NOT LIB_RC EQUAL 0)
        set(LIB_FAILED "building library '${LIB_MODULE}' failed\n${LIB_ERR}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT LIB_FAILED STREQUAL "")
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: ${LIB_FAILED}")
    continue()
  endif()

  set(EXE "${WORK_DIR}/${NAME}")
  execute_process(
    COMMAND "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
            ${IMPORT_ARGS} -o "${EXE}" "${CASE}"
    RESULT_VARIABLE BUILD_RC
    OUTPUT_VARIABLE BUILD_OUT
    ERROR_VARIABLE BUILD_ERR)

  # --- cases that are meant to fail to compile ---------------------------
  if(NOT EXPECT_ERROR STREQUAL "")
    if(BUILD_RC EQUAL 0)
      math(EXPR FAILED "${FAILED}+1")
      list(APPEND FAILURES "${NAME}: expected compilation to fail")
    elseif(NOT "${BUILD_ERR}" MATCHES "${EXPECT_ERROR}")
      math(EXPR FAILED "${FAILED}+1")
      list(APPEND FAILURES
        "${NAME}: expected the error to mention '${EXPECT_ERROR}'\n${BUILD_ERR}")
    else()
      math(EXPR PASSED "${PASSED}+1")
      message(STATUS "ok       ${NAME} (rejected as expected)")
    endif()
    continue()
  endif()

  if(NOT BUILD_RC EQUAL 0)
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: compilation failed\n${BUILD_ERR}")
    continue()
  endif()

  execute_process(
    COMMAND "${EXE}"
    RESULT_VARIABLE RUN_RC
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR)

  # --- cases that are meant to panic at run time -------------------------
  if(NOT EXPECT_PANIC STREQUAL "")
    if(RUN_RC EQUAL 0)
      math(EXPR FAILED "${FAILED}+1")
      list(APPEND FAILURES "${NAME}: expected a runtime panic, but it exited 0")
    elseif(NOT "${RUN_ERR}" MATCHES "${EXPECT_PANIC}")
      math(EXPR FAILED "${FAILED}+1")
      list(APPEND FAILURES
        "${NAME}: expected the panic to mention '${EXPECT_PANIC}'\n${RUN_ERR}")
    else()
      math(EXPR PASSED "${PASSED}+1")
      message(STATUS "ok       ${NAME} (panicked as expected)")
    endif()
    continue()
  endif()

  if(NOT RUN_RC EQUAL 0)
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: exited with ${RUN_RC}\n${RUN_ERR}")
    continue()
  endif()

  # A leak report on stderr fails the case: reference counting must balance.
  if("${RUN_ERR}" MATCHES "still live at exit")
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: leaked objects\n${RUN_ERR}")
    continue()
  endif()

  string(REGEX REPLACE "\n$" "" RUN_OUT "${RUN_OUT}")
  string(REPLACE "\n" ";" ACTUAL "${RUN_OUT}")
  string(REPLACE ";" "\n" EXPECT_TEXT "${EXPECT_LINES}")
  string(REPLACE ";" "\n" ACTUAL_TEXT "${ACTUAL}")

  if(NOT "${ACTUAL_TEXT}" STREQUAL "${EXPECT_TEXT}")
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES
      "${NAME}: output mismatch\n--- expected ---\n${EXPECT_TEXT}\n--- actual ---\n${ACTUAL_TEXT}")
  else()
    math(EXPR PASSED "${PASSED}+1")
    message(STATUS "ok       ${NAME}")
  endif()
endforeach()

message(STATUS "")
message(STATUS "${PASSED} passed, ${FAILED} failed")
if(FAILED GREATER 0)
  foreach(F ${FAILURES})
    message(STATUS "")
    message(STATUS "FAILED ${F}")
  endforeach()
  message(FATAL_ERROR "${FAILED} test case(s) failed")
endif()
