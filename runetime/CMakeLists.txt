# The memory core of the runtime, written in Rune and compiled by runec.
#
# It is built after the compiler exists, then archived together with the
# remaining C parts into libruneruntime.a — so a Rune program links one
# library and never has to know which half a symbol came from.

set(RUNETIME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core.rune)
set(RUNETIME_OBJ ${CMAKE_CURRENT_BINARY_DIR}/runetime_core.o)

# On macOS the runtime's C half is compiled for the deployment target, and
# this half has to say the same: an object marked for the build machine's
# own, newer macOS would make every program linked with it warn — or refuse
# — on an older one.
set(RUNETIME_TARGET)
if(APPLE AND CMAKE_OSX_DEPLOYMENT_TARGET)
  set(RUNETIME_ARCH ${CMAKE_SYSTEM_PROCESSOR})
  if(RUNETIME_ARCH STREQUAL "aarch64")
    set(RUNETIME_ARCH arm64)
  endif()
  set(RUNETIME_TARGET --target ${RUNETIME_ARCH}-apple-macosx${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

add_custom_command(
  OUTPUT ${RUNETIME_OBJ}
  COMMAND $<TARGET_FILE:runec> --no-stdlib -O2 ${RUNETIME_TARGET} -o ${RUNETIME_OBJ} ${RUNETIME_SRC}
  DEPENDS runec ${RUNETIME_SRC}
  COMMENT "Compiling the Rune half of the runtime (runetime/core.rune)"
  VERBATIM)

add_custom_target(runetime_core DEPENDS ${RUNETIME_OBJ})
set(RUNETIME_OBJECT ${RUNETIME_OBJ} PARENT_SCOPE)
