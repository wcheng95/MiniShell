#include "sdkconfig.h"

#if !CONFIG_FATFS_LFN_HEAP
#error "ADV requires CONFIG_FATFS_LFN_HEAP=y; regenerate sdkconfig from sdkconfig.defaults"
#endif

#if CONFIG_FATFS_MAX_LFN < 255
#error "ADV requires CONFIG_FATFS_MAX_LFN=255"
#endif

#if !CONFIG_FATFS_API_ENCODING_UTF_8
#error "ADV requires CONFIG_FATFS_API_ENCODING_UTF_8=y"
#endif

/* Compile-time configuration guard only. */
