get_filename_component(JS8_TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(js8_phy_core STATIC
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_crc.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_ldpc.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_channel.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_frame.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_protocol_frame.c)
target_include_directories(js8_phy_core PUBLIC ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine)
target_compile_options(js8_phy_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(js8_phy_core PUBLIC m)
add_executable(js8_phy_unit ${JS8_TEST_ROOT}/tests/js8_phy_test.c)
target_link_libraries(js8_phy_unit PRIVATE js8_phy_core)
target_compile_options(js8_phy_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
add_test(NAME js8_phy_unit COMMAND js8_phy_unit)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME js8_ldpc_tables COMMAND ${Python3_EXECUTABLE}
    ${JS8_TEST_ROOT}/tests/js8_ldpc_tables_test.py ${JS8_TEST_ROOT})
foreach(boundary dependency platform)
    add_test(NAME js8chat_${boundary}_boundary COMMAND ${Python3_EXECUTABLE}
        ${JS8_TEST_ROOT}/tests/app_${boundary}_boundary.py ${JS8_TEST_ROOT} js8chat)
endforeach()

add_library(js8_rx_core STATIC
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_monitor.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_decoder.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/vendor/kissfft/kiss_fft.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/vendor/kissfft/kiss_fftr.c)
target_include_directories(js8_rx_core PUBLIC ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine)
target_include_directories(js8_rx_core PRIVATE ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/vendor/kissfft)
target_compile_options(js8_rx_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(js8_rx_core PUBLIC js8_phy_core m)
add_executable(js8_rx_unit ${JS8_TEST_ROOT}/tests/js8_rx_test.c)
target_link_libraries(js8_rx_unit PRIVATE js8_rx_core)
target_compile_options(js8_rx_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
add_test(NAME js8_rx_unit COMMAND js8_rx_unit)
add_test(NAME js8_no_heap COMMAND ${Python3_EXECUTABLE}
    ${JS8_TEST_ROOT}/tests/js8_no_heap_test.py ${CMAKE_NM}
    $<TARGET_FILE:js8_rx_core> $<TARGET_FILE:js8_phy_core>)

add_executable(js8_frame_unit ${JS8_TEST_ROOT}/tests/js8_frame_test.c)
target_link_libraries(js8_frame_unit PRIVATE js8_phy_core)
target_compile_options(js8_frame_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
add_test(NAME js8_frame_unit COMMAND js8_frame_unit)

add_executable(js8_protocol_frame_unit ${JS8_TEST_ROOT}/tests/js8_protocol_frame_test.c)
target_link_libraries(js8_protocol_frame_unit PRIVATE js8_phy_core)
target_compile_options(js8_protocol_frame_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
add_test(NAME js8_protocol_frame_unit COMMAND js8_protocol_frame_unit)
