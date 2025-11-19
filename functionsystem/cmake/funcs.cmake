# 获取 git 信息
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

# 获取 git 提交哈希值
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

# 安装目标函数
function(INSTALL_TARGET target)
    install(TARGETS ${target}
            ARCHIVE DESTINATION ${INSTALL_LIBDIR}
            LIBRARY DESTINATION ${INSTALL_LIBDIR}
            RUNTIME DESTINATION ${INSTALL_BINDIR})
endfunction()