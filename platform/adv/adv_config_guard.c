#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "ADV requires ESP32-S3 target"
#endif

#if !CONFIG_FATFS_LFN_HEAP
#error "ADV requires CONFIG_FATFS_LFN_HEAP=y; regenerate sdkconfig from sdkconfig.defaults"
#endif

#if CONFIG_FATFS_MAX_LFN < 255
#error "ADV requires CONFIG_FATFS_MAX_LFN=255"
#endif

#if !CONFIG_FATFS_API_ENCODING_UTF_8
#error "ADV requires CONFIG_FATFS_API_ENCODING_UTF_8=y"
#endif

#if CONFIG_FATFS_PER_FILE_CACHE
#error "ADV requires shared FATFS sector cache; CONFIG_FATFS_PER_FILE_CACHE must be disabled"
#endif

#if CONFIG_ESP_SYSTEM_MEMPROT
#error "ADV runtime ELF requires executable SRAM; CONFIG_ESP_SYSTEM_MEMPROT must be disabled"
#endif

/* Compile-time configuration guard only. */
