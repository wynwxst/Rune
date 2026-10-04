# Compiles, links and runs every tests/cases/*.rune, comparing what it prints
# against the expectations written in its header:
#
#   // EXPECT: <line of stdout>      one per expected line, in order
#   // EXPECT-PANIC: <substring>     the program must abort, stderr must match
#   // EXPECT-ERROR: <substring>     compilation must fail with this message
#   // LIB: <module> <path>...       build <path>s into <module>.rul first
#   // FLAGS: <args...>              extra `runec` flags for this case
#   // WITH: <path>                  another source compiled with this case,
#                                    which is how a macro package is brought in
#   // CXX: <path> [flags...]        compile <path> (under `cases/`) as C++
#                                    and link the object into the case
#   // JOBS: <n>                     compile the case with RUNE_JOBS=<n>, for
#                                    what only shows on one thread (or many)
#
# `@LLVM_LIBDIR@` and `@LLVM_INCLUDE_DIR@` in a `// FLAGS:` or `// CXX:` line
# stand for the LLVM this compiler was built against, so a case can call
# into LLVM's own C++ API; `@LLVM_LINK@` in a `// FLAGS:` line, for the
# `runec` flags that link against it, however it was built, and
# `@LLVM_MAJOR@` for its major version, for an API that changed. A case that needs a C++ compiler is skipped, with a
# note, when CMake found none.
#
# A `// LIB:` line names a Rune library the case imports. Its source is a path
# under `cases/`, and lives in `cases/lib/` so that the glob above — which only
# looks at the top level — does not pick it up as a case of its own. Each is
# compiled with `--emit-lib` into a directory of the case's own, which is then
# handed to the case as `-I`. Several may be given, and are built in order, so
# one library may import another.
#
# Invoked by CTest with RUNEC, RUNTIME_LIB, STDLIB_DIR, CASE_DIR and WORK_DIR,
# and optionally CXX_COMPILER, LLVM_LIBDIR, LLVM_INCLUDE_DIR, LLVM_LINK and LLVM_MAJOR.

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
  set(CASE_SHARED "")
  set(CASE_WITH "")
  set(CASE_CXX "")
  set(CASE_JOBS "")
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
      string(REPLACE "@LLVM_LIBDIR@" "${LLVM_LIBDIR}" ONE_FLAGS "${ONE_FLAGS}")
      string(REPLACE "@LLVM_INCLUDE_DIR@" "${LLVM_INCLUDE_DIR}" ONE_FLAGS "${ONE_FLAGS}")
      string(REPLACE "@LLVM_LINK@" "${LLVM_LINK}" ONE_FLAGS "${ONE_FLAGS}")
      string(REPLACE "@LLVM_MAJOR@" "${LLVM_MAJOR}" ONE_FLAGS "${ONE_FLAGS}")
      separate_arguments(ONE_FLAGS)
      list(APPEND CASE_FLAGS ${ONE_FLAGS})
    elseif(LINE MATCHES "^// WITH: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" ONE_WITH)
      list(APPEND CASE_WITH "${CASE_DIR}/${ONE_WITH}")
    elseif(LINE MATCHES "^// SHARED: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" ONE_SHARED)
      list(APPEND CASE_SHARED "${ONE_SHARED}")
    elseif(LINE MATCHES "^// JOBS: ?([0-9]+)")
      set(CASE_JOBS "${CMAKE_MATCH_1}")
    elseif(LINE MATCHES "^// CXX: ?(.*)$")
      string(STRIP "${CMAKE_MATCH_1}" ONE_CXX)
      string(REPLACE "@LLVM_LIBDIR@" "${LLVM_LIBDIR}" ONE_CXX "${ONE_CXX}")
      string(REPLACE "@LLVM_INCLUDE_DIR@" "${LLVM_INCLUDE_DIR}" ONE_CXX "${ONE_CXX}")
      list(APPEND CASE_CXX "${ONE_CXX}")
    endif()
  endforeach()

  set(SAFETY_ARGS "")
  if(NOT CASE_SAFETY STREQUAL "")
    set(SAFETY_ARGS --safety "${CASE_SAFETY}")
  endif()
  list(APPEND SAFETY_ARGS ${CASE_FLAGS})

  # --- the C++ half, compiled before the case and linked into it ----------
  set(CXX_FAILED "")
  set(CXX_SKIPPED "")
  if(NOT CASE_CXX STREQUAL "")
    if(NOT DEFINED CXX_COMPILER OR CXX_COMPILER STREQUAL "")
      set(CXX_SKIPPED "no C++ compiler was found when this build was configured")
    endif()
    set(CXX_INDEX 0)
    foreach(SPEC ${CASE_CXX})
      if(NOT CXX_SKIPPED STREQUAL "")
        break()
      endif()
      separate_arguments(SPEC_PARTS UNIX_COMMAND "${SPEC}")
      list(GET SPEC_PARTS 0 CXX_SOURCE)
      list(REMOVE_AT SPEC_PARTS 0)
      set(CXX_OBJ "${WORK_DIR}/${NAME}.cxx${CXX_INDEX}.o")
      math(EXPR CXX_INDEX "${CXX_INDEX}+1")
      execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++17 -c -O1 -fno-exceptions -fno-rtti
                ${SPEC_PARTS} -o "${CXX_OBJ}" "${CASE_DIR}/${CXX_SOURCE}"
        RESULT_VARIABLE CXX_RC
        OUTPUT_VARIABLE CXX_OUT
        ERROR_VARIABLE CXX_ERR)
      if(NOT CXX_RC EQUAL 0)
        set(CXX_FAILED "compiling '${CXX_SOURCE}' failed\n${CXX_ERR}")
        break()
      endif()
      list(APPEND SAFETY_ARGS --link-arg "${CXX_OBJ}")
    endforeach()
  endif()
  if(NOT CXX_SKIPPED STREQUAL "")
    message(STATUS "skip     ${NAME} (${CXX_SKIPPED})")
    continue()
  endif()
  if(NOT CXX_FAILED STREQUAL "")
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: ${CXX_FAILED}")
    continue()
  endif()

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
      # Several sources may follow, for a library with more than one file.
      separate_arguments(LIB_PATHS UNIX_COMMAND "${CMAKE_MATCH_2}")
      set(LIB_SOURCE "")
      foreach(LIB_PATH ${LIB_PATHS})
        list(APPEND LIB_SOURCE "${CASE_DIR}/${LIB_PATH}")
      endforeach()
      execute_process(
        COMMAND "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
                ${IMPORT_ARGS} --emit-lib --module "${LIB_MODULE}"
                -o "${LIB_DIR}/${LIB_MODULE}.rul" ${LIB_SOURCE}
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

  # --- native shared libraries this case loads at run time -----------------
  # `// SHARED: <name> <source.rune>` builds one, and the case is run with
  # `RUNE_TEST_SHARED_<NAME>` naming the file — which is how a program finds
  # something it is going to `dlopen` rather than link against.
  set(SHARED_ENV "")
  set(SHARED_FAILED "")
  foreach(SPEC ${CASE_SHARED})
    string(REGEX MATCH "^([^ \t]+)[ \t]+(.+)$" _m "${SPEC}")
    if(NOT _m)
      set(SHARED_FAILED "malformed `// SHARED:` line: ${SPEC}")
      break()
    endif()
    set(SHARED_NAME "${CMAKE_MATCH_1}")
    set(SHARED_SOURCE "${CASE_DIR}/${CMAKE_MATCH_2}")
    if(APPLE)
      set(SHARED_FILE "${WORK_DIR}/lib${SHARED_NAME}.dylib")
    elseif(WIN32)
      set(SHARED_FILE "${WORK_DIR}/${SHARED_NAME}.dll")
    else()
      set(SHARED_FILE "${WORK_DIR}/lib${SHARED_NAME}.so")
    endif()
    execute_process(
      COMMAND "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
              ${IMPORT_ARGS} --shared -o "${SHARED_FILE}" "${SHARED_SOURCE}"
      RESULT_VARIABLE SHARED_RC
      OUTPUT_VARIABLE SHARED_OUT
      ERROR_VARIABLE SHARED_ERR)
    if(NOT SHARED_RC EQUAL 0)
      set(SHARED_FAILED "building shared library '${SHARED_NAME}' failed\n${SHARED_ERR}")
      break()
    endif()
    string(TOUPPER "${SHARED_NAME}" SHARED_UPPER)
    list(APPEND SHARED_ENV "RUNE_TEST_SHARED_${SHARED_UPPER}=${SHARED_FILE}")
  endforeach()
  if(NOT SHARED_FAILED STREQUAL "")
    math(EXPR FAILED "${FAILED}+1")
    list(APPEND FAILURES "${NAME}: ${SHARED_FAILED}")
    continue()
  endif()

  set(EXE "${WORK_DIR}/${NAME}")
  set(BUILD_ENV "")
  if(NOT CASE_JOBS STREQUAL "")
    set(BUILD_ENV ${CMAKE_COMMAND} -E env "RUNE_JOBS=${CASE_JOBS}")
  endif()
  execute_process(
    COMMAND ${BUILD_ENV} "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
            ${IMPORT_ARGS} -o "${EXE}" "${CASE}" ${CASE_WITH}
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

  # Whatever compiles under Zombie — the default — must compile under ARC
  # too: counting can share where single ownership has to move or borrow,
  # so it accepts more, never less. A case with libraries is left out, since
  # those were built for the case's own memory model.
  string(JOIN " " FLAGS_TEXT ${CASE_FLAGS})
  if(NOT FLAGS_TEXT MATCHES "--memory arc"
     AND CASE_LIBS STREQUAL "" AND CASE_SHARED STREQUAL "")
    execute_process(
      COMMAND ${BUILD_ENV} "${RUNEC}" --no-color --stdlib "${STDLIB_DIR}" ${SAFETY_ARGS}
              --memory arc --check "${CASE}" ${CASE_WITH}
      RESULT_VARIABLE ARC_RC
      OUTPUT_VARIABLE ARC_OUT
      ERROR_VARIABLE ARC_ERR)
    if(NOT ARC_RC EQUAL 0)
      math(EXPR FAILED "${FAILED}+1")
      list(APPEND FAILURES
        "${NAME}: compiles under Zombie but not under ARC\n${ARC_ERR}")
      continue()
    endif()
  endif()

  set(RUN_COMMAND "${EXE}")
  if(NOT SHARED_ENV STREQUAL "")
    set(RUN_COMMAND ${CMAKE_COMMAND} -E env ${SHARED_ENV} "${EXE}")
  endif()
  execute_process(
    COMMAND ${RUN_COMMAND}
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
