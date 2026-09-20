cmake_minimum_required(VERSION 3.16)
project(minishell VERSION 0.1.0 LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "minishell reference build targets Linux. "
        "This build directory appears to contain a non-Linux toolchain cache. "
        "Use a fresh build directory, for example: cmake -S . -B build-linux")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(MINISHELL_SERVICE_SOURCES
    core/minishell_services/services.c
    core/minishell_services/system_service.c
    core/minishell_services/console_service.c
    core/minishell_services/memory_service.c
    core/minishell_services/filesystem_service.c
    core/minishell_services/filesystem_paths.c
    core/minishell_services/filesystem_handles.c
    core/minishell_services/filesystem_quota.c
    core/minishell_services/time_location_service.c
    core/minishell_services/display_service.c
    core/minishell_services/input_service.c
    core/minishell_services/audio_service.c
    core/minishell_services/digital_io_service.c
    core/minishell_services/serial_service.c
)

set(MINISHELL_LINUX_SOURCES
    platform/linux/main.c
    platform/linux/linux_backend.c
    platform/linux/linux_common.c
    platform/linux/linux_console.c
    platform/linux/linux_filesystem.c
    platform/linux/linux_time_location.c
    platform/linux/linux_terminal.c
    platform/linux/linux_terminal_parser.c
    platform/linux/linux_loader.c
    platform/linux/linux_services.c
    platform/linux/linux_serial.c
    platform/linux/linux_audio_wav.c
    platform/linux/linux_digital_io.c
    platform/linux/platform_info.c
    platform/linux/resource_config.c
)

add_executable(minishell
    core/minishell_runtime.c
    core/shell.c
    core/alias.c
    core/app_manager.c
    ${MINISHELL_SERVICE_SOURCES}
    ${MINISHELL_LINUX_SOURCES}
)

target_include_directories(minishell PRIVATE
    include
    core
    core/minishell_services
    platform/linux
)

target_link_libraries(minishell PRIVATE ${CMAKE_DL_LIBS})
target_link_options(minishell PRIVATE "-Wl,--export-dynamic-symbol=mini_api_get")
target_compile_options(minishell PRIVATE -Wall -Wextra -Wpedantic)

function(add_minishell_app target)
    add_library(${target} MODULE ${ARGN})
    target_include_directories(${target} PRIVATE
        include
        apps/${target}/main
    )
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${target}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/runtime/apps"
    )
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endfunction()

add_minishell_app(hello apps/hello/hello.c)
add_minishell_app(cat apps/cat/main/cat.c)
add_minishell_app(cp
    apps/cp/main/cp.c
    apps/cp/main/cp_copy.c
)
add_minishell_app(date apps/date/main/date.c)
add_minishell_app(df apps/df/main/df.c)
add_minishell_app(free apps/free/main/free.c)
add_minishell_app(ls apps/ls/main/ls.c)
add_minishell_app(mkdir apps/mkdir/main/mkdir.c)
add_minishell_app(mv apps/mv/main/mv.c)
add_minishell_app(rm apps/rm/main/rm.c)
add_minishell_app(rmdir apps/rmdir/main/rmdir.c)
add_minishell_app(nano
    apps/nano/main/nano.c
    apps/nano/main/nano_buffer.c
    apps/nano/main/nano_file.c
    apps/nano/main/nano_ui.c
    apps/nano/main/nano_util.c
)

add_minishell_app(ft8
    apps/ft8/main/ft8_main.c
    apps/ft8/main/ft8_ui_adapter.c
    apps/ft8/src/app_controller/app_controller.c
    apps/ft8/src/app_controller/app_controller_instance.c
    apps/ft8/src/app_controller/app_controller_cat_test.c
    apps/ft8/src/app_controller/app_controller_tx.c
    apps/ft8/src/app_controller/app_controller_tx_physical.c
    apps/ft8/src/app_controller/app_tx_schedule.c
    apps/ft8/src/app_controller/app_rx_order.c
    apps/ft8/src/auto_seq/auto_seq.c
    apps/ft8/src/auto_seq/auto_seq_tx_intent.c
    apps/ft8/src/tx_offset/tx_offset.c
    apps/ft8/src/config_service/config_service.c
    apps/ft8/src/radio_control/radio_control.c
    apps/ft8/src/radio_control/radio_qmx.c
    apps/ft8/src/presentation_profile/presentation_profile.c
    apps/ft8/src/storage_service/storage_service.c
    apps/ft8/src/log_service/log_service.c
    apps/ft8/src/tx_lifecycle/tx_lifecycle.c
    apps/ft8/src/tx_encoder/tx_encoder.c
    apps/ft8/src/tx_encoder/tx_channel.c
    apps/ft8/src/ui_shell/ui_shell.c
    apps/ft8/src/rx_audio_adapter/rx_audio_adapter.c
    apps/ft8/src/rx_frontend/rx_frontend.c
    apps/ft8/src/rx_slot_framer/rx_slot_framer.c
    apps/ft8/src/rx_result_builder/rx_result_builder.c
    apps/ft8/src/ft8_engine/ft8_engine.c
    apps/ft8/src/ft8_engine/ft8_monitor.c
    apps/ft8/src/ft8_engine/ft8_decoder.c
    apps/ft8/src/ft8_engine/ft8_ldpc.c
    apps/ft8/src/ft8_engine/ft8_crc.c
    apps/ft8/src/ft8_engine/ft8_hash_store.c
    apps/ft8/src/ft8_engine/ft8_message_codec.c
    apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fft.c
    apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fftr.c
)
set_source_files_properties(
    apps/ft8/src/app_controller/app_controller.c
    apps/ft8/src/app_controller/app_controller_tx.c
    PROPERTIES COMPILE_DEFINITIONS FT8_APP_CONTROLLER_INTERNAL
)
target_include_directories(ft8 PRIVATE
    apps/ft8/include
    apps/ft8/src/app_controller
    apps/ft8/src/auto_seq
    apps/ft8/src/tx_offset
    apps/ft8/src/config_service
    apps/ft8/src/radio_control
    apps/ft8/src/presentation_profile
    apps/ft8/src/storage_service
    apps/ft8/src/log_service
    apps/ft8/src/tx_lifecycle
    apps/ft8/src/tx_encoder
    apps/ft8/src/ui_shell
    apps/ft8/src/rx_audio_adapter
    apps/ft8/src/rx_frontend
    apps/ft8/src/rx_slot_framer
    apps/ft8/src/rx_result_builder
    apps/ft8/src/ft8_engine
    apps/ft8/src/ft8_engine/vendor/kissfft
)
target_link_libraries(ft8 PRIVATE m)
target_compile_definitions(ft8 PRIVATE
    FT8_DEFAULT_PRESENTATION=FT8_PRESENTATION_ADV
    FT8_DEFAULT_RX_ENDPOINT="alsa:hw:2,0"
)

# Exercise the production parser with the actual Linux target definitions and
# with no composition defaults. Discard the unused app entry/transport code.
foreach(profile linux portable)
    add_executable(ft8_options_${profile}_unit tests/ft8_options_test.c
        apps/ft8/src/presentation_profile/presentation_profile.c)
    target_include_directories(ft8_options_${profile}_unit PRIVATE
        $<TARGET_PROPERTY:ft8,INCLUDE_DIRECTORIES>)
    target_compile_options(ft8_options_${profile}_unit PRIVATE
        -Wall -Wextra -Wpedantic -ffunction-sections)
    target_link_options(ft8_options_${profile}_unit PRIVATE -Wl,--gc-sections)
    add_test(NAME ft8_options_${profile}_unit COMMAND ft8_options_${profile}_unit)
endforeach()
target_compile_definitions(ft8_options_linux_unit PRIVATE
    $<TARGET_PROPERTY:ft8,COMPILE_DEFINITIONS> FT8_TEST_LINUX_DEFAULTS=1)

add_executable(ft8_radio_control_unit tests/ft8_radio_control_test.c
    apps/ft8/src/radio_control/radio_control.c
    apps/ft8/src/radio_control/radio_qmx.c
    apps/ft8/src/config_service/config_service.c)
target_include_directories(ft8_radio_control_unit PRIVATE include
    apps/ft8/src/radio_control apps/ft8/src/config_service)
target_compile_options(ft8_radio_control_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(ft8_radio_control_unit PRIVATE m)
add_test(NAME ft8_radio_control_unit COMMAND ft8_radio_control_unit)

add_executable(ft8_radio_tx_unit tests/ft8_radio_tx_test.c
    apps/ft8/src/radio_control/radio_control.c
    apps/ft8/src/radio_control/radio_qmx.c
    apps/ft8/src/app_controller/app_controller_cat_test.c
    apps/ft8/src/storage_service/storage_service.c
    apps/ft8/src/config_service/config_service.c)
target_include_directories(ft8_radio_tx_unit PRIVATE $<TARGET_PROPERTY:ft8,INCLUDE_DIRECTORIES>)
target_compile_options(ft8_radio_tx_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
target_link_libraries(ft8_radio_tx_unit PRIVATE m)
add_test(NAME ft8_radio_tx_unit COMMAND ft8_radio_tx_unit)

add_executable(linux_serial_unit tests/linux_serial_test.c ${MINISHELL_SERVICE_SOURCES}
    platform/linux/linux_serial.c platform/linux/linux_common.c)
target_include_directories(linux_serial_unit PRIVATE include core core/minishell_services platform/linux)
target_compile_options(linux_serial_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME linux_serial_unit COMMAND linux_serial_unit)
set_tests_properties(linux_serial_unit PROPERTIES TIMEOUT 10)

add_library(private_import_probe MODULE tests/apps/private_import_probe.c)
set_target_properties(private_import_probe PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(private_import_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(service_probe MODULE tests/apps/service_probe.c)
target_include_directories(service_probe PRIVATE include)
set_target_properties(service_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "service_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(service_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(input_probe MODULE tests/apps/input_probe.c)
target_include_directories(input_probe PRIVATE include)
set_target_properties(input_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "input_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(input_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(dir_probe MODULE tests/apps/dir_probe.c)
target_include_directories(dir_probe PRIVATE include)
set_target_properties(dir_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "dir_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(dir_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(resource_probe MODULE tests/apps/resource_probe.c)
target_include_directories(resource_probe PRIVATE include)
set_target_properties(resource_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "resource_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(resource_probe PRIVATE -Wall -Wextra -Wpedantic)

add_library(audio_probe MODULE tests/apps/audio_probe.c)
target_include_directories(audio_probe PRIVATE include)
set_target_properties(audio_probe PROPERTIES
    PREFIX ""
    OUTPUT_NAME "audio_probe"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test-apps"
)
target_compile_options(audio_probe PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ft8_log_service_unit
    tests/ft8_log_service_test.c
    apps/ft8/src/config_service/config_service.c
    apps/ft8/src/log_service/log_service.c
)
target_include_directories(ft8_log_service_unit PRIVATE include apps/ft8/src/log_service apps/ft8/src/config_service)
target_compile_options(ft8_log_service_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_storage_service_unit
    tests/ft8_storage_service_test.c
    apps/ft8/src/storage_service/storage_service.c
)
target_include_directories(ft8_storage_service_unit PRIVATE
    include
    apps/ft8/src/storage_service
)
target_compile_options(ft8_storage_service_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_runtime_grid_ownership_unit
    tests/ft8_runtime_grid_ownership_test.c
    apps/ft8/src/app_controller/app_controller_instance.c
    apps/ft8/src/config_service/config_service.c
)
target_include_directories(ft8_runtime_grid_ownership_unit PRIVATE
    include
    apps/ft8/include
    apps/ft8/src/app_controller
    apps/ft8/src/auto_seq
    apps/ft8/src/config_service
    apps/ft8/src/radio_control
    apps/ft8/src/storage_service
    apps/ft8/src/log_service
    apps/ft8/src/tx_lifecycle
)
target_compile_options(ft8_runtime_grid_ownership_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

add_executable(ft8_ui_smoke
    tests/ft8_ui_smoke.c
    apps/ft8/src/presentation_profile/presentation_profile.c
    apps/ft8/src/ui_shell/ui_shell.c
)
target_include_directories(ft8_ui_smoke PRIVATE
    apps/ft8/include
    apps/ft8/src/presentation_profile
    apps/ft8/src/ui_shell
)
target_compile_options(ft8_ui_smoke PRIVATE -Wall -Wextra -Wpedantic)

add_executable(linux_terminal_parser_unit
    tests/linux_terminal_parser_test.c
    platform/linux/linux_terminal_parser.c
)
target_include_directories(linux_terminal_parser_unit PRIVATE
    include
    platform/linux
)
target_compile_options(linux_terminal_parser_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

add_library(ft8_auto_seq_core STATIC
    apps/ft8/src/auto_seq/auto_seq.c
)
target_include_directories(ft8_auto_seq_core PUBLIC
    apps/ft8/src/auto_seq
)
target_compile_options(ft8_auto_seq_core PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_auto_seq_as2_unit tests/ft8_auto_seq_as2_test.c)
target_link_libraries(ft8_auto_seq_as2_unit PRIVATE ft8_auto_seq_core)
target_compile_options(ft8_auto_seq_as2_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_tx_lifecycle_as7_unit
    tests/ft8_tx_lifecycle_as7_test.c
    apps/ft8/src/app_controller/app_controller_tx.c
    apps/ft8/src/auto_seq/auto_seq_tx_intent.c
    apps/ft8/src/tx_lifecycle/tx_lifecycle.c
)
target_include_directories(ft8_tx_lifecycle_as7_unit PRIVATE
    include
    apps/ft8/include
    apps/ft8/src/app_controller
    apps/ft8/src/auto_seq
    apps/ft8/src/config_service
    apps/ft8/src/radio_control
    apps/ft8/src/storage_service
    apps/ft8/src/log_service
    apps/ft8/src/tx_lifecycle
)
target_link_libraries(ft8_tx_lifecycle_as7_unit PRIVATE ft8_auto_seq_core)
target_compile_options(ft8_tx_lifecycle_as7_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

set(FT8_MONITOR_SOURCES
    apps/ft8/src/ft8_engine/ft8_monitor.c
    apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fft.c
    apps/ft8/src/ft8_engine/vendor/kissfft/kiss_fftr.c
)

add_library(ft8_monitor_core STATIC ${FT8_MONITOR_SOURCES})
target_include_directories(ft8_monitor_core PUBLIC
    apps/ft8/src/ft8_engine
    apps/ft8/src/ft8_engine/vendor/kissfft
)
target_compile_options(ft8_monitor_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ft8_monitor_core PUBLIC m)

set(FT8_DECODER_SOURCES
    apps/ft8/src/ft8_engine/ft8_decoder.c
    apps/ft8/src/ft8_engine/ft8_ldpc.c
    apps/ft8/src/ft8_engine/ft8_crc.c
)

add_library(ft8_decoder_core STATIC ${FT8_DECODER_SOURCES})
target_include_directories(ft8_decoder_core PUBLIC
    apps/ft8/src/ft8_engine
)
target_compile_options(ft8_decoder_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ft8_decoder_core PUBLIC m)

add_library(ft8_hash_store_core STATIC
    apps/ft8/src/ft8_engine/ft8_hash_store.c
)
target_include_directories(ft8_hash_store_core PUBLIC
    apps/ft8/src/ft8_engine
)
target_compile_options(ft8_hash_store_core PRIVATE -Wall -Wextra -Wpedantic)

add_library(ft8_protocol_core STATIC
    apps/ft8/src/ft8_engine/ft8_message_codec.c
)
target_include_directories(ft8_protocol_core PUBLIC
    apps/ft8/src/ft8_engine
)
target_compile_options(ft8_protocol_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ft8_protocol_core PUBLIC ft8_hash_store_core)

add_library(ft8_tx_encoder_core STATIC
    apps/ft8/src/tx_encoder/tx_encoder.c
    apps/ft8/src/tx_encoder/tx_channel.c
    apps/ft8/src/ft8_engine/ft8_crc.c)
target_include_directories(ft8_tx_encoder_core PUBLIC
    apps/ft8/src/tx_encoder apps/ft8/src/auto_seq)
target_link_libraries(ft8_tx_encoder_core PUBLIC ft8_protocol_core)
target_compile_options(ft8_tx_encoder_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_executable(ft8_tx_encoder_unit tests/ft8_tx_encoder_test.c)
target_link_libraries(ft8_tx_encoder_unit PRIVATE ft8_tx_encoder_core ft8_decoder_core)
target_compile_options(ft8_tx_encoder_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_tx_encoder_unit COMMAND ft8_tx_encoder_unit)

add_library(ft8_engine_core STATIC
    apps/ft8/src/ft8_engine/ft8_engine.c
)
target_include_directories(ft8_engine_core PUBLIC
    apps/ft8/src/ft8_engine
)
target_compile_options(ft8_engine_core PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(ft8_engine_core PUBLIC
    ft8_monitor_core
    ft8_decoder_core
    ft8_protocol_core
)

add_executable(ft8_decode
    apps/ft8/tools/ft8_decode.c
)
target_link_libraries(ft8_decode PRIVATE ft8_engine_core)
target_compile_options(ft8_decode PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_monitor_rx1c_unit tests/ft8_monitor_rx1c_test.c)
target_link_libraries(ft8_monitor_rx1c_unit PRIVATE ft8_monitor_core)
target_compile_options(ft8_monitor_rx1c_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_decoder_rx1d_unit tests/ft8_decoder_rx1d_test.c)
target_link_libraries(ft8_decoder_rx1d_unit PRIVATE ft8_decoder_core)
target_compile_options(ft8_decoder_rx1d_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_hash_store_rx1e_unit tests/ft8_hash_store_rx1e_test.c)
target_link_libraries(ft8_hash_store_rx1e_unit PRIVATE ft8_hash_store_core)
target_compile_options(ft8_hash_store_rx1e_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_message_codec_rx1f_unit tests/ft8_message_codec_rx1f_test.c)
target_link_libraries(ft8_message_codec_rx1f_unit PRIVATE ft8_protocol_core)
target_compile_options(ft8_message_codec_rx1f_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(ft8_engine_rx1g_unit tests/ft8_engine_rx1g_test.c)
target_link_libraries(ft8_engine_rx1g_unit PRIVATE ft8_engine_core)
target_compile_options(ft8_engine_rx1g_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(adv_audio_tx_timeout_unit tests/adv_audio_tx_timeout_test.c)
target_include_directories(adv_audio_tx_timeout_unit PRIVATE include platform/adv)
target_compile_options(adv_audio_tx_timeout_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(linux_audio_discontinuity_unit tests/linux_audio_discontinuity_test.c)
target_include_directories(linux_audio_discontinuity_unit PRIVATE
    include core/minishell_services platform/linux)
target_link_libraries(linux_audio_discontinuity_unit PRIVATE dl pthread)
target_compile_options(linux_audio_discontinuity_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(linux_alsa_discontinuity_unit tests/linux_alsa_discontinuity_test.c)
target_include_directories(linux_alsa_discontinuity_unit PRIVATE include core/minishell_services)
target_link_libraries(linux_alsa_discontinuity_unit PRIVATE dl)
target_compile_options(linux_alsa_discontinuity_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

# Exercise the same production RX stack, exposing private state only in the test TU.
get_target_property(FT8_DISCONTINUITY_SOURCES ft8 SOURCES)
list(FILTER FT8_DISCONTINUITY_SOURCES EXCLUDE REGEX "apps/ft8/main/|app_controller/app_controller\\.c$")
get_target_property(FT8_DISCONTINUITY_INCLUDES ft8 INCLUDE_DIRECTORIES)
add_executable(ft8_rx_discontinuity_unit tests/ft8_rx_discontinuity_test.c ${FT8_DISCONTINUITY_SOURCES})
target_include_directories(ft8_rx_discontinuity_unit PRIVATE ${FT8_DISCONTINUITY_INCLUDES})
target_link_libraries(ft8_rx_discontinuity_unit PRIVATE m)
target_compile_options(ft8_rx_discontinuity_unit PRIVATE -Wall -Wextra -Wpedantic)

add_executable(ft8_rx_adv_profile_unit tests/ft8_rx_discontinuity_test.c ${FT8_DISCONTINUITY_SOURCES})
target_include_directories(ft8_rx_adv_profile_unit PRIVATE ${FT8_DISCONTINUITY_INCLUDES})
target_compile_definitions(ft8_rx_adv_profile_unit PRIVATE FT8_DEFAULT_FREQ_OSR=1 FT8_TEST_EXPECT_FREQ_OSR=1)
target_link_libraries(ft8_rx_adv_profile_unit PRIVATE m)
target_compile_options(ft8_rx_adv_profile_unit PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME ft8_rx_adv_profile_unit COMMAND ft8_rx_adv_profile_unit)

add_executable(rx_slot_framer_rx4_unit tests/rx_slot_framer_rx4_test.c
    apps/ft8/src/rx_slot_framer/rx_slot_framer.c)
target_include_directories(rx_slot_framer_rx4_unit PRIVATE apps/ft8/src/rx_slot_framer)
target_compile_options(rx_slot_framer_rx4_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)

add_executable(rx_audio_adapter_rx6_unit
    tests/rx_audio_adapter_rx6_test.c
    apps/ft8/src/rx_audio_adapter/rx_audio_adapter.c
)
target_include_directories(rx_audio_adapter_rx6_unit PRIVATE
    include
    apps/ft8/src/rx_audio_adapter
)
target_compile_options(rx_audio_adapter_rx6_unit PRIVATE
    -Wall -Wextra -Werror -Wpedantic
)

set(FT8_RX1C_REFERENCE_WAV "" CACHE FILEPATH
    "Optional pinned MiniFT8-V2 6 kHz RX-1A golden WAV")
if(FT8_RX1C_REFERENCE_WAV)
    if(NOT EXISTS "${FT8_RX1C_REFERENCE_WAV}")
        message(FATAL_ERROR "FT8_RX1C_REFERENCE_WAV does not exist: ${FT8_RX1C_REFERENCE_WAV}")
    endif()
    add_executable(ft8_monitor_rx1c_reference tests/ft8_monitor_rx1c_reference.c)
    target_link_libraries(ft8_monitor_rx1c_reference PRIVATE ft8_monitor_core)
    target_compile_options(ft8_monitor_rx1c_reference PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

set(FT8_RX1D_REFERENCE_WAV "" CACHE FILEPATH
    "Optional pinned MiniFT8-V2 6 kHz RX-1A golden WAV for decoder regression")
if(FT8_RX1D_REFERENCE_WAV)
    if(NOT EXISTS "${FT8_RX1D_REFERENCE_WAV}")
        message(FATAL_ERROR "FT8_RX1D_REFERENCE_WAV does not exist: ${FT8_RX1D_REFERENCE_WAV}")
    endif()
    add_executable(ft8_decoder_rx1d_reference tests/ft8_decoder_rx1d_reference.c)
    target_link_libraries(ft8_decoder_rx1d_reference PRIVATE ft8_monitor_core ft8_decoder_core)
    target_compile_options(ft8_decoder_rx1d_reference PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

set(FT8_RX1G_REFERENCE_WAV "" CACHE FILEPATH
    "Optional pinned MiniFT8-V2 6 kHz RX-1A golden WAV for pure engine regression")
if(FT8_RX1G_REFERENCE_WAV)
    if(NOT EXISTS "${FT8_RX1G_REFERENCE_WAV}")
        message(FATAL_ERROR "FT8_RX1G_REFERENCE_WAV does not exist: ${FT8_RX1G_REFERENCE_WAV}")
    endif()
    add_executable(ft8_engine_rx1g_reference tests/ft8_engine_rx1g_reference.c)
    target_link_libraries(ft8_engine_rx1g_reference PRIVATE ft8_engine_core)
    target_compile_options(ft8_engine_rx1g_reference PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME linux_export_boundary
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_export_boundary.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE:private_import_probe>
            $<TARGET_FILE:hello>
)

add_test(
    NAME linux_smoke
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_smoke.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_services
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_services.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:service_probe>
)

add_test(
    NAME linux_input
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_input.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:input_probe>
)

add_test(
    NAME linux_portable_apps
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_portable_apps.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_nano
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_nano.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
)

add_test(
    NAME linux_directory
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_directory.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
            $<TARGET_FILE_DIR:dir_probe>
)

add_test(
    NAME linux_resources
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_resources.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:hello>
            $<TARGET_FILE_DIR:resource_probe>
)

add_test(
    NAME linux_audio
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_audio.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:audio_probe>
            ${CMAKE_SOURCE_DIR}/tests/kfs16b12k.wav
)

add_test(NAME ft8_log_service_unit COMMAND ft8_log_service_unit)
add_test(NAME ft8_storage_service_unit COMMAND ft8_storage_service_unit)
add_test(NAME ft8_runtime_grid_ownership_unit COMMAND ft8_runtime_grid_ownership_unit)
add_test(NAME ft8_ui_smoke COMMAND ft8_ui_smoke)
add_test(NAME linux_terminal_parser_unit COMMAND linux_terminal_parser_unit)
add_test(NAME ft8_auto_seq_as2_unit COMMAND ft8_auto_seq_as2_unit)
add_test(NAME ft8_tx_lifecycle_as7_unit COMMAND ft8_tx_lifecycle_as7_unit)
add_test(NAME ft8_monitor_rx1c_unit COMMAND ft8_monitor_rx1c_unit)
add_test(NAME ft8_decoder_rx1d_unit COMMAND ft8_decoder_rx1d_unit)
add_test(NAME ft8_hash_store_rx1e_unit COMMAND ft8_hash_store_rx1e_unit)
add_test(NAME ft8_message_codec_rx1f_unit COMMAND ft8_message_codec_rx1f_unit)
add_test(NAME ft8_engine_rx1g_unit COMMAND ft8_engine_rx1g_unit)
add_test(NAME adv_audio_tx_timeout_unit COMMAND adv_audio_tx_timeout_unit)
add_test(NAME linux_audio_discontinuity_unit COMMAND linux_audio_discontinuity_unit)
add_test(NAME linux_alsa_discontinuity_unit COMMAND linux_alsa_discontinuity_unit)
set_tests_properties(linux_alsa_discontinuity_unit PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME ft8_rx_discontinuity_unit COMMAND ft8_rx_discontinuity_unit)
add_test(NAME rx_slot_framer_rx4_unit COMMAND rx_slot_framer_rx4_unit)
add_test(NAME rx_audio_adapter_rx6_unit COMMAND rx_audio_adapter_rx6_unit)

if(FT8_RX1C_REFERENCE_WAV)
    add_test(
        NAME ft8_monitor_rx1c_reference
        COMMAND ft8_monitor_rx1c_reference "${FT8_RX1C_REFERENCE_WAV}"
    )
endif()

if(FT8_RX1D_REFERENCE_WAV)
    add_test(
        NAME ft8_decoder_rx1d_reference
        COMMAND ft8_decoder_rx1d_reference "${FT8_RX1D_REFERENCE_WAV}"
    )
endif()

if(FT8_RX1G_REFERENCE_WAV)
    add_test(
        NAME ft8_engine_rx1g_reference
        COMMAND ft8_engine_rx1g_reference "${FT8_RX1G_REFERENCE_WAV}"
    )
    add_test(NAME ft8_engine_freq1_reference
        COMMAND ft8_engine_rx1g_reference "${FT8_RX1G_REFERENCE_WAV}" 1)
endif()

add_test(
    NAME linux_ft8_config_load
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_ft8_config_load.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:ft8>
)

add_test(
    NAME linux_ft8
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_SOURCE_DIR}/tests/linux_ft8.py
            $<TARGET_FILE:minishell>
            $<TARGET_FILE_DIR:ft8>
)

foreach(app ft8 keyer)
    add_test(
        NAME architecture_app_${app}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app_dependency_boundary.py
                ${CMAKE_SOURCE_DIR} ${app}
    )
    add_test(
        NAME architecture_platform_${app}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app_platform_boundary.py
                ${CMAKE_SOURCE_DIR} ${app}
    )
endforeach()
add_test(NAME linux_ft8_cat COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/linux_ft8_cat.py $<TARGET_FILE:minishell> $<TARGET_FILE_DIR:ft8>)
set_tests_properties(linux_ft8_cat PROPERTIES TIMEOUT 30)
add_test(NAME linux_ft8_cat_tx COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/linux_ft8_cat_tx.py $<TARGET_FILE:minishell> $<TARGET_FILE_DIR:ft8>)
set_tests_properties(linux_ft8_cat_tx PROPERTIES TIMEOUT 20)
add_test(NAME serial_protocol_boundary COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/tests/serial_protocol_boundary.py ${CMAKE_SOURCE_DIR})
add_test(
    NAME architecture_app_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app_dependency_boundary.py --self-test
)
add_test(
    NAME architecture_platform_selftest
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/app_platform_boundary.py --self-test
)

add_test(
    NAME ft8_reference_pin
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ft8_reference_pin.py
            ${CMAKE_SOURCE_DIR} --self-test
)

add_test(
    NAME ft8_ldpc_tables
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ft8_ldpc_tables_test.py
            ${CMAKE_SOURCE_DIR}
)

add_executable(adv_filesystem_rename_unit
    tests/adv_filesystem_rename_test.c
    apps/ft8/src/storage_service/storage_service.c
)
target_include_directories(adv_filesystem_rename_unit PRIVATE
    include platform/adv apps/ft8/src/storage_service
)
target_compile_options(adv_filesystem_rename_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_filesystem_rename_unit COMMAND adv_filesystem_rename_unit)

add_executable(adv_audio_uac_buffer_unit tests/adv_audio_uac_buffer_test.c)
target_include_directories(adv_audio_uac_buffer_unit PRIVATE platform/adv)
target_compile_options(adv_audio_uac_buffer_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_audio_uac_buffer_unit COMMAND adv_audio_uac_buffer_unit)
add_test(NAME adv_uac_driver_patch
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_uac_driver_patch_test.py
            ${CMAKE_C_COMPILER})

add_executable(adv_usb_console_handoff_unit tests/adv_usb_console_handoff_test.c)
target_include_directories(adv_usb_console_handoff_unit PRIVATE platform/adv)
target_compile_options(adv_usb_console_handoff_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_usb_console_handoff_unit COMMAND adv_usb_console_handoff_unit)
add_test(NAME adv_usb_console_boundary
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_usb_console_boundary.py)
add_test(NAME adv_uac_allocation
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_uac_allocation_test.py
            ${CMAKE_CXX_COMPILER})
add_test(NAME adv_power_apps
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_power_apps_test.py)

target_include_directories(ft8_tx_lifecycle_as7_unit PRIVATE apps/ft8/src/tx_encoder)
target_include_directories(ft8_runtime_grid_ownership_unit PRIVATE apps/ft8/src/tx_encoder apps/ft8/src/auto_seq)

add_executable(ft8_tx_schedule_unit tests/ft8_tx_schedule_test.c
    apps/ft8/src/app_controller/app_tx_schedule.c)
target_include_directories(ft8_tx_schedule_unit PRIVATE apps/ft8/src/app_controller
    apps/ft8/src/tx_encoder apps/ft8/src/auto_seq)
target_compile_options(ft8_tx_schedule_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_tx_schedule_unit COMMAND ft8_tx_schedule_unit)

add_executable(ft8_physical_tx_unit tests/ft8_physical_tx_test.c ${FT8_DISCONTINUITY_SOURCES})
target_include_directories(ft8_physical_tx_unit PRIVATE $<TARGET_PROPERTY:ft8,INCLUDE_DIRECTORIES>)
target_link_libraries(ft8_physical_tx_unit PRIVATE m)
target_compile_options(ft8_physical_tx_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_physical_tx_unit COMMAND ft8_physical_tx_unit)
add_test(NAME ft8_qso_unit COMMAND ft8_physical_tx_unit --qso)
add_test(NAME ft8_band_cat_unit COMMAND ft8_physical_tx_unit --band-cat)

add_executable(ft8_tx_offset_unit tests/ft8_tx_offset_test.c
    apps/ft8/src/tx_offset/tx_offset.c apps/ft8/src/config_service/config_service.c)
target_include_directories(ft8_tx_offset_unit PRIVATE apps/ft8/src/tx_offset
    apps/ft8/src/config_service apps/ft8/src/auto_seq apps/ft8/src/tx_encoder)
target_compile_options(ft8_tx_offset_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_tx_offset_unit COMMAND ft8_tx_offset_unit)

add_executable(shell_alias_unit tests/shell_alias_test.c core/alias.c)
target_include_directories(shell_alias_unit PRIVATE include core core/minishell_services)
target_compile_options(shell_alias_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME shell_alias_unit COMMAND shell_alias_unit)
add_test(NAME linux_aliases COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/linux_aliases.py
    $<TARGET_FILE:minishell> $<TARGET_FILE_DIR:hello>)

add_executable(ft8_rx_result_builder_unit tests/rx_result_builder_rx5_test.c
    apps/ft8/src/rx_result_builder/rx_result_builder.c)
target_include_directories(ft8_rx_result_builder_unit PRIVATE apps/ft8/src/rx_result_builder)
target_link_libraries(ft8_rx_result_builder_unit PRIVATE ft8_protocol_core)
target_compile_options(ft8_rx_result_builder_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_rx_result_builder_unit COMMAND ft8_rx_result_builder_unit)

add_executable(ft8_rx_order_unit tests/ft8_rx_order_test.c
    apps/ft8/src/app_controller/app_rx_order.c)
target_include_directories(ft8_rx_order_unit PRIVATE $<TARGET_PROPERTY:ft8,INCLUDE_DIRECTORIES>)
target_compile_options(ft8_rx_order_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME ft8_rx_order_unit COMMAND ft8_rx_order_unit)

add_test(NAME adv_ft8_defaults
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_ft8_defaults_test.py
            ${CMAKE_C_COMPILER})
add_test(NAME adv_qmx_serial
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_qmx_serial_test.py
            ${CMAKE_CXX_COMPILER})

add_executable(adv_webfs_unit tests/adv_webfs_test.c platform/adv/adv_webfs_logic.c)
target_include_directories(adv_webfs_unit PRIVATE include platform/adv)
target_compile_options(adv_webfs_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_webfs_unit COMMAND adv_webfs_unit)

add_executable(adv_webfs_mutations_unit tests/adv_webfs_mutations_test.c platform/adv/adv_webfs_logic.c)
target_include_directories(adv_webfs_mutations_unit PRIVATE include platform/adv)
target_compile_options(adv_webfs_mutations_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_webfs_mutations_unit COMMAND adv_webfs_mutations_unit)
add_test(NAME adv_webfs_http
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_webfs_http_test.py ${CMAKE_SOURCE_DIR})
find_program(WEBFS_NODE_EXECUTABLE node)
if(WEBFS_NODE_EXECUTABLE)
    add_test(NAME adv_webfs_page
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_webfs_page_test.py ${CMAKE_SOURCE_DIR})
endif()


add_executable(adv_filesystem_space_unit tests/adv_filesystem_space_test.c)
target_include_directories(adv_filesystem_space_unit PRIVATE include platform/adv)
target_compile_options(adv_filesystem_space_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_filesystem_space_unit COMMAND adv_filesystem_space_unit)

add_test(NAME adv_usb_host_owner
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_usb_host_owner_test.py
            ${CMAKE_CXX_COMPILER})

add_executable(adv_webfs_settings_unit tests/adv_webfs_settings_test.c platform/adv/adv_webfs_settings.c)
target_include_directories(adv_webfs_settings_unit PRIVATE include platform/adv)
target_compile_options(adv_webfs_settings_unit PRIVATE -Wall -Wextra -Werror -Wpedantic)
add_test(NAME adv_webfs_settings_unit COMMAND adv_webfs_settings_unit)
add_test(NAME adv_webfs_wifi_settings
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/adv_webfs_wifi_settings_test.py ${CMAKE_SOURCE_DIR})
