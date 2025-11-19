aux_source_directory(${CMAKE_CURRENT_LIST_DIR} GLOBAL_SCHEDULER_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/scheduler_manager GLOBAL_SCHEDULER_MANAGER_SRCS)

set(GLOBAL_SCHEDULER_LIB global_scheduler_lib)
set(GLOBAL_SCHEDULER_LIB ${GLOBAL_SCHEDULER_LIB} PARENT_SCOPE)
set_property(GLOBAL PROPERTY GLOBAL_SCHEDULER_DIR "${CMAKE_CURRENT_LIST_DIR}")
set_property(GLOBAL PROPERTY GLOBAL_SCHEDULER_LIB ${GLOBAL_SCHEDULER_LIB})

add_library(${GLOBAL_SCHEDULER_LIB} STATIC
        ${GLOBAL_SCHEDULER_SRCS}
        ${GLOBAL_SCHEDULER_MANAGER_SRCS})
set_target_properties(${GLOBAL_SCHEDULER_LIB} PROPERTIES UNITY_BUILD ON)
target_compile_options(${GLOBAL_SCHEDULER_LIB} PRIVATE -fPIE)

target_include_directories(${GLOBAL_SCHEDULER_LIB} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
add_dependencies(${GLOBAL_SCHEDULER_LIB} domain_scheduler_lib scheduler_topology)
target_link_libraries(${GLOBAL_SCHEDULER_LIB} PUBLIC scheduler_topology domain_scheduler_lib)