#include "sdkconfig.h"

#if !CONFIG_IDF_TARGET_ESP32S3
#error "ADV requires ESP32-S3 target"
#endif

#if !defined(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240) || CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ != 240
#error "ADV requires a 240 MHz CPU; regenerate sdkconfig from sdkconfig.defaults"
#endif

#if !defined(CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE) || CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE < 2048
#error "ADV QMX enumeration requires CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE >= 2048; regenerate sdkconfig from sdkconfig.defaults"
#endif

#if !defined(CONFIG_UAC_NUM_ISOC_URBS) || CONFIG_UAC_NUM_ISOC_URBS != 3
#error "ADV UAC experiment requires CONFIG_UAC_NUM_ISOC_URBS=3; regenerate sdkconfig from sdkconfig.defaults"
#endif

#if !defined(CONFIG_UAC_NUM_PACKETS_PER_URB) || CONFIG_UAC_NUM_PACKETS_PER_URB != 12
#error "ADV UAC experiment requires CONFIG_UAC_NUM_PACKETS_PER_URB=12; regenerate sdkconfig from sdkconfig.defaults"
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

#if defined(CONFIG_ESP_SYSTEM_MEMPROT_FEATURE) || defined(CONFIG_ESP_SYSTEM_MEMPROT)
#error "ADV runtime ELF requires executable SRAM; ESP-IDF memory protection must be disabled"
#endif

/* Compile-time configuration guard only. */
