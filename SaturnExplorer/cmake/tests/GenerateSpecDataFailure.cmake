# Does GenerateSpecData.cmake (the se-regen-sla step) fail when it cannot publish the header?
# OUT_FILE is made a directory, which copy_if_different "copies" into with exit code 0, so this
# pins that the script checks what actually landed rather than trusting the exit code.
# Inputs (-D): GENERATOR (path to GenerateSpecData.cmake), SPEC_DIR, WORK (scratch directory).
file(REMOVE_RECURSE "${WORK}")
set(out "${WORK}/Sh2SpecData.h")
file(MAKE_DIRECTORY "${out}")
execute_process(COMMAND ${CMAKE_COMMAND} -DSPEC_DIR=${SPEC_DIR} -DOUT_FILE=${out} -P ${GENERATOR}
                RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
if(rc EQUAL 0)
    message(FATAL_ERROR "GenerateSpecData reported success although ${out} could not be written")
endif()
if(NOT EXISTS "${out}.tmp")
    message(FATAL_ERROR "GenerateSpecData failed but removed the generated ${out}.tmp")
endif()
message(STATUS "GenerateSpecData failed as it should: ${err}")
file(REMOVE_RECURSE "${WORK}")
