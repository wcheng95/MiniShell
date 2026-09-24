#ifndef SSTV_BMP_H
#define SSTV_BMP_H

#include "minishell/api.h"
#include "sstv_core.h"

typedef struct {
    const mini_fs_api_t *fs;
    const char *path;
    mini_file_t file;
    unsigned rows;
    int active;
    int failed;
    uint8_t bgr[3u * SSTV_ROBOT36_WIDTH];
} SstvBmpSink;

void sstv_bmp_sink_init(SstvBmpSink *bmp, const mini_fs_api_t *fs, const char *path,
                        SstvImageSink *sink);
void sstv_bmp_abort(SstvBmpSink *bmp);

#endif
