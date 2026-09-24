#ifndef RTTY_WAV_H
#define RTTY_WAV_H
#include "minishell/api.h"
/* NULL on success; static Console-ready diagnostic otherwise. */
const char *rtty_wav_decode(const mini_fs_api_t *fs, const mini_console_api_t *console, const char *path);
#endif
