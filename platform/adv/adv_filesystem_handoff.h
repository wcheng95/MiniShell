#pragma once

#include <stdbool.h>

#include "sdmmc_cmd.h"
#include "wear_levelling.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool has_flash;
    wl_handle_t flash_wl;
    bool has_sd;
    sdmmc_card_t *sd_card;
} adv_filesystem_handoff_t;

/*
 * Temporarily transfer selected raw FAT media out of normal MiniShell VFS
 * ownership. The filesystem subsystem remains the hardware owner: it unmounts
 * normal VFS access, initializes raw handles for the borrower, then restores
 * the normal mounts in adv_filesystem_handoff_end().
 */
int adv_filesystem_handoff_begin(bool use_flash,
                                 bool use_sd,
                                 adv_filesystem_handoff_t *out_handoff);
int adv_filesystem_handoff_end(void);

#ifdef __cplusplus
}
#endif
