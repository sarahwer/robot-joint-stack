# Chip-independent libraries. They compile unchanged for the PC (unit tests)
# and for every microcontroller port.

add_library(rjs_hal STATIC hal/src/can_timing.c)
target_include_directories(rjs_hal PUBLIC hal/include)
target_compile_options(rjs_hal PRIVATE ${RJS_WARNINGS})

add_library(rjs_protocol STATIC protocol/src/protocol.c)
target_include_directories(rjs_protocol PUBLIC protocol/include)
target_link_libraries(rjs_protocol PUBLIC rjs_hal)
target_compile_options(rjs_protocol PRIVATE ${RJS_WARNINGS})

add_library(rjs_node STATIC node/src/node_monitor.c)
target_include_directories(rjs_node PUBLIC node/include)
target_link_libraries(rjs_node PUBLIC rjs_protocol)
target_compile_options(rjs_node PRIVATE ${RJS_WARNINGS})

add_library(rjs_joint STATIC joint/src/joint.c)
target_include_directories(rjs_joint PUBLIC joint/include)
target_link_libraries(rjs_joint PUBLIC rjs_protocol)
target_compile_options(rjs_joint PRIVATE ${RJS_WARNINGS})
