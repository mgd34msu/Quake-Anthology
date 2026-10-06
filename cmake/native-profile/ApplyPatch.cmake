find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${QA_PATCH}"
    WORKING_DIRECTORY "${QA_SOURCE_DIR}" RESULT_VARIABLE qa_patch_check
    OUTPUT_QUIET ERROR_QUIET)
if(qa_patch_check EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${QA_PATCH}"
        WORKING_DIRECTORY "${QA_SOURCE_DIR}" RESULT_VARIABLE qa_patch_apply)
    if(NOT qa_patch_apply EQUAL 0)
        message(FATAL_ERROR "Applying the native monitor segment patch failed")
    endif()
else()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${QA_PATCH}"
        WORKING_DIRECTORY "${QA_SOURCE_DIR}" RESULT_VARIABLE qa_patch_present
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT qa_patch_present EQUAL 0)
        message(FATAL_ERROR "Native monitor source does not match its segment patch")
    endif()
endif()
