# The memory core of the runtime, written in Rune and compiled by runec.
#
# It is built after the compiler exists, then archived together with the
# remaining C parts into libruneruntime.a — so a Rune program links one
# library and never has to know which half a symbol came from.

set(RUNETIME_SRC ${CMAKE_CURRENT_SOURCE_DIR}/core.rune)
set(RUNETIME_OBJ ${CMAKE_CURRENT_BINARY_DIR}/runetime_core.o)

add_custom_command(
  OUTPUT ${RUNETIME_OBJ}
  COMMAND $<TARGET_FILE:runec> --no-stdlib -O2 -o ${RUNETIME_OBJ} ${RUNETIME_SRC}
  DEPENDS runec ${RUNETIME_SRC}
  COMMENT "Compiling the Rune half of the runtime (runetime/core.rune)"
  VERBATIM)

add_custom_target(runetime_core DEPENDS ${RUNETIME_OBJ})
set(RUNETIME_OBJECT ${RUNETIME_OBJ} PARENT_SCOPE)
