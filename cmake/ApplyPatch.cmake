foreach(requiredVariable IN ITEMS GIT_EXECUTABLE SOURCE_DIR PATCH_FILE)
    if(NOT DEFINED ${requiredVariable} OR "${${requiredVariable}}" STREQUAL "")
        message(FATAL_ERROR "${requiredVariable} is required")
    endif()
endforeach()

execute_process(
    COMMAND
        "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" apply --check "${PATCH_FILE}"
    RESULT_VARIABLE patchCheckResult
    ERROR_VARIABLE patchCheckError
)

if(patchCheckResult EQUAL 0)
    execute_process(
        COMMAND
            "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" apply --whitespace=nowarn
            "${PATCH_FILE}"
        RESULT_VARIABLE patchResult
        ERROR_VARIABLE patchError
    )
    if(NOT patchResult EQUAL 0)
        message(FATAL_ERROR "Could not apply ${PATCH_FILE}:\n${patchError}")
    endif()
    return()
endif()

execute_process(
    COMMAND
        "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" apply --reverse --check "${PATCH_FILE}"
    RESULT_VARIABLE reverseCheckResult
    ERROR_VARIABLE reverseCheckError
)

if(reverseCheckResult EQUAL 0)
    message(STATUS "Patch already applied: ${PATCH_FILE}")
    return()
endif()

message(
    FATAL_ERROR
    "Patch cannot be applied and is not already present: ${PATCH_FILE}\n"
    "Apply check:\n${patchCheckError}\n"
    "Reverse check:\n${reverseCheckError}"
)
