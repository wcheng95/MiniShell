cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(minishell_adv)

# T017: retain the registry component and compile only its RX-loss notification
# patch from the build directory. Hash validation fails closed on source drift.
idf_component_get_property(uac_lib espressif__usb_host_uac COMPONENT_LIB)
idf_component_get_property(uac_dir espressif__usb_host_uac COMPONENT_DIR)
set(uac_patch "${CMAKE_CURRENT_SOURCE_DIR}/patch_uac_rx.py")
set(uac_original "${uac_dir}/uac_host.c")
set(uac_patched "${CMAKE_BINARY_DIR}/t017_uac/uac_host.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${uac_patch}" "${uac_original}")
execute_process(COMMAND "${PYTHON}" "${uac_patch}" "${uac_original}" "${uac_patched}"
    RESULT_VARIABLE patch_result)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "T017 UAC RX loss patch failed")
endif()
get_target_property(uac_sources ${uac_lib} SOURCES)
set(uac_replaced FALSE)
set(uac_build_sources "")
foreach(source IN LISTS uac_sources)
    get_filename_component(source_name "${source}" NAME)
    if(source_name STREQUAL "uac_host.c")
        list(APPEND uac_build_sources "${uac_patched}")
        set(uac_replaced TRUE)
    else()
        list(APPEND uac_build_sources "${source}")
    endif()
endforeach()
if(NOT uac_replaced)
    message(FATAL_ERROR "T017 UAC component source layout changed")
endif()
set_property(TARGET ${uac_lib} PROPERTY SOURCES "${uac_build_sources}")
message(STATUS "T017: UAC 1.3.3 RX losses report TRANSFER_ERROR (build-local patch)")

