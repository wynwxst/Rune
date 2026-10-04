# Builds tools/rune-ffi's `interface` unit and each of its tests the way
# `rune ffi` builds the tool — the unit as a library, the program against it
# and libclang — and runs them.
#
# Invoked by CTest with RUNEC, STDLIB_DIR, SOURCE_DIR, WORK_DIR, LIBCLANG_DIR.
file(MAKE_DIRECTORY "${WORK_DIR}")
file(GLOB UNIT "${SOURCE_DIR}/tools/rune-ffi/src/interface/*.rune")
execute_process(
  COMMAND "${RUNEC}" --memory zombie --stdlib "${STDLIB_DIR}" --emit-lib
          --module interface -o "${WORK_DIR}/interface.rul" ${UNIT}
  RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "building the interface unit failed")
endif()
file(GLOB TESTS "${SOURCE_DIR}/tools/rune-ffi/tests/*.rune")
foreach(t ${TESTS})
  get_filename_component(name "${t}" NAME_WE)
  set(exe "${WORK_DIR}/${name}")
  execute_process(
    COMMAND "${RUNEC}" --memory zombie --stdlib "${STDLIB_DIR}" -I "${WORK_DIR}"
            -L "${LIBCLANG_DIR}" --link-arg "-Wl,-rpath,${LIBCLANG_DIR}" -o "${exe}" "${t}"
    RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "building ${name} failed")
  endif()
  execute_process(COMMAND "${exe}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "${name} failed")
  endif()
endforeach()
