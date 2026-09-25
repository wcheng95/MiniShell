#ifndef SSTV_WAV_H
#define SSTV_WAV_H
#include "minishell/api.h"
const char *sstv_wav_decode(const mini_fs_api_t *fs, const char *input_path, const char *output_path);
#endif
