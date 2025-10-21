# for git information
macro(get_git_branch git_branch_out_var)
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} symbolic-ref --short -q HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                ERROR_QUIET
                OUTPUT_VARIABLE ${git_branch_out_var}
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif ()
endmacro()

macro(get_git_hash git_hash_out_var)
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(
                COMMAND ${GIT_EXECUTABLE} log -1 "--pretty=format:[%H] [%ai]"
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                ERROR_QUIET
                OUTPUT_VARIABLE ${git_hash_out_var}
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )
    endif ()
endmacro()