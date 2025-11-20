project(yaml_tool)

add_library(yaml_tool SHARED
    yaml_tool.cpp
)

add_dependencies(yaml_tool yaml)
target_link_libraries(yaml_tool ${yaml_LIB})

INSTALL_TARGET(yaml_tool)