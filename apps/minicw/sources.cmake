get_filename_component(MINICW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MINICW_SOURCES
    "${MINICW_ROOT}/apps/minicw/src/app_core/app_core.c"
    "${MINICW_ROOT}/apps/minicw/src/audio_service/audio_service.c"
    "${MINICW_ROOT}/apps/minicw/src/keyer_service/keyer_decoder.c"
    "${MINICW_ROOT}/apps/minicw/src/keyer_service/keyer_service.c"
    "${MINICW_ROOT}/apps/minicw/src/port/minicw_port.c"
    "${MINICW_ROOT}/apps/minicw/src/runtime/minicw_libc.c"
    "${MINICW_ROOT}/apps/minicw/src/ui_service/ui_screen.c"
    "${MINICW_ROOT}/apps/minicw/src/ui_service/ui_service.c"
)
set(MINICW_INCLUDES "${MINICW_ROOT}/include"
    "${MINICW_ROOT}/apps/minicw/src/app_core"
    "${MINICW_ROOT}/apps/minicw/src/audio_service"
    "${MINICW_ROOT}/apps/minicw/src/keyer_service"
    "${MINICW_ROOT}/apps/minicw/src/port"
    "${MINICW_ROOT}/apps/minicw/src/runtime"
    "${MINICW_ROOT}/apps/minicw/src/ui_service"
)
