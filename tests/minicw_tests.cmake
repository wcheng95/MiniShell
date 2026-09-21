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
target_sources(minicw_runtime_unit PRIVATE
    ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_link_libraries(minicw_runtime_unit PRIVATE m)
target_include_directories(minicw_runtime_unit PRIVATE ${MINICW_INCLUDES}
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(minicw_runtime_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
add_test(NAME minicw_runtime_unit COMMAND minicw_runtime_unit)
add_executable(tone_stream_unit ${MINICW_TEST_ROOT}/tests/tone_stream_test.c)
target_include_directories(tone_stream_unit PRIVATE ${MINICW_TEST_ROOT}/include)
target_compile_options(tone_stream_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
target_link_libraries(tone_stream_unit PRIVATE m)
add_test(NAME tone_stream_unit COMMAND tone_stream_unit)
file(GLOB MINICW_TEST_SERVICES "${MINICW_TEST_ROOT}/core/minishell_services/*.c")
add_executable(tone_service_unit ${MINICW_TEST_ROOT}/tests/tone_service_test.c
    ${MINICW_TEST_SERVICES}
    ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_include_directories(tone_service_unit PRIVATE ${MINICW_TEST_ROOT}/include
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(tone_service_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -UNDEBUG)
target_link_libraries(tone_service_unit PRIVATE m)
add_test(NAME tone_service_unit COMMAND tone_service_unit)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME adv_tone_worker COMMAND ${Python3_EXECUTABLE}
    ${MINICW_TEST_ROOT}/tests/adv_tone_worker_test.py ${MINICW_TEST_ROOT})
add_test(NAME tone_reference COMMAND ${Python3_EXECUTABLE}
    ${MINICW_TEST_ROOT}/tests/tone_reference_test.py ${MINICW_TEST_ROOT})
set(MINICW_PERSISTENCE_SOURCES ${MINICW_SOURCES})
list(FILTER MINICW_PERSISTENCE_SOURCES EXCLUDE REGEX "/(app_core/app_core|port/minicw_port)\\.c$")
add_executable(minicw_persistence_unit ${MINICW_TEST_ROOT}/tests/minicw_persistence_test.c
    ${MINICW_PERSISTENCE_SOURCES} ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_include_directories(minicw_persistence_unit PRIVATE ${MINICW_INCLUDES}
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(minicw_persistence_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
target_link_libraries(minicw_persistence_unit PRIVATE m)
add_test(NAME minicw_persistence_unit COMMAND minicw_persistence_unit)
add_executable(minicw_ui_io_unit ${MINICW_TEST_ROOT}/tests/minicw_ui_io_test.c
    ${MINICW_PERSISTENCE_SOURCES} ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_include_directories(minicw_ui_io_unit PRIVATE ${MINICW_INCLUDES}
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(minicw_ui_io_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
target_link_libraries(minicw_ui_io_unit PRIVATE m)
add_test(NAME minicw_ui_io_unit COMMAND minicw_ui_io_unit)
add_executable(minicw_lookup_unit ${MINICW_TEST_ROOT}/tests/minicw_lookup_test.c
    ${MINICW_PERSISTENCE_SOURCES} ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_include_directories(minicw_lookup_unit PRIVATE ${MINICW_INCLUDES}
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(minicw_lookup_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
target_link_libraries(minicw_lookup_unit PRIVATE m)
add_test(NAME minicw_lookup_unit COMMAND minicw_lookup_unit)
set(MINICW_TRANSCRIPT_SOURCES ${MINICW_PERSISTENCE_SOURCES})
list(FILTER MINICW_TRANSCRIPT_SOURCES EXCLUDE REGEX "/app_core/transcript\\.c$")
add_executable(minicw_transcript_unit ${MINICW_TEST_ROOT}/tests/minicw_transcript_test.c
    ${MINICW_TRANSCRIPT_SOURCES} ${MINICW_TEST_ROOT}/platform/common/tone_stream.c
    ${MINICW_TEST_ROOT}/platform/common/tone_sim.c)
target_include_directories(minicw_transcript_unit PRIVATE ${MINICW_INCLUDES}
    ${MINICW_TEST_ROOT}/core/minishell_services ${MINICW_TEST_ROOT}/platform/common)
target_compile_options(minicw_transcript_unit PRIVATE -Wall -Wextra -Werror -Wpedantic -fno-builtin -UNDEBUG)
target_link_libraries(minicw_transcript_unit PRIVATE m)
add_test(NAME minicw_transcript_unit COMMAND minicw_transcript_unit)
