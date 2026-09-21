get_filename_component(MINICW_TEST_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include(${MINICW_TEST_ROOT}/apps/minicw/sources.cmake)
add_executable(minicw_domain_unit
    ${MINICW_TEST_ROOT}/tests/minicw_domain_test.c
    ${MINICW_TEST_ROOT}/apps/minicw/src/keyer_service/keyer_service.c
    ${MINICW_TEST_ROOT}/apps/minicw/src/keyer_service/keyer_decoder.c
    ${MINICW_TEST_ROOT}/apps/minicw/src/audio_service/audio_service.c
    ${MINICW_TEST_ROOT}/apps/minicw/src/runtime/minicw_libc.c)
target_include_directories(minicw_domain_unit PRIVATE ${MINICW_INCLUDES})
target_compile_options(minicw_domain_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
add_test(NAME minicw_domain_unit COMMAND minicw_domain_unit)
add_executable(minicw_runtime_unit ${MINICW_TEST_ROOT}/tests/minicw_runtime_test.c ${MINICW_SOURCES})
target_include_directories(minicw_runtime_unit PRIVATE ${MINICW_INCLUDES})
target_compile_options(minicw_runtime_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
add_test(NAME minicw_runtime_unit COMMAND minicw_runtime_unit)
