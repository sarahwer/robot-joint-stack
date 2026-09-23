# Host unit tests, built with the PC compiler.
# RJS_SANITIZE=ON adds AddressSanitizer + UndefinedBehaviorSanitizer (used in CI).

option(RJS_SANITIZE "Build tests with ASan/UBSan" OFF)

set(RJS_TESTS test_protocol test_node_monitor test_joint test_can_timing test_text_screen)

foreach(t ${RJS_TESTS})
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE rjs_protocol rjs_node rjs_joint rjs_ui)
    target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
    if(RJS_SANITIZE)
        target_compile_options(${t} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${t} PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME ${t} COMMAND ${t})
endforeach()

if(RJS_SANITIZE)
    foreach(lib rjs_hal rjs_protocol rjs_node rjs_joint rjs_ui)
        target_compile_options(${lib} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    endforeach()
endif()

# C encoder -> Python decoder: both implementations must agree on the wire format.
find_package(Python3 COMPONENTS Interpreter)
add_executable(dump_frames dump_frames.c)
target_link_libraries(dump_frames PRIVATE rjs_protocol)
if(RJS_SANITIZE)
    target_compile_options(dump_frames PRIVATE -fsanitize=address,undefined)
    target_link_options(dump_frames PRIVATE -fsanitize=address,undefined)
endif()
if(Python3_FOUND)
    add_test(NAME c_python_sync
             COMMAND sh -c "$<TARGET_FILE:dump_frames> | ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_c_python_sync.py")
    add_test(NAME python_decoder COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_rjs_protocol.py)
endif()
