get_filename_component(JS8_TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(js8_phy_core STATIC
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_crc.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_ldpc.c
    ${JS8_TEST_ROOT}/apps/js8chat/src/js8_engine/js8_channel.c)
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
