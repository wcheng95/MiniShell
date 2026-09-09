/*
 *  Copyright (c) 2003-2004, Mark Borgerding. All rights reserved.
 *  This file is part of KISS FFT - https://github.com/mborgerding/kissfft
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef KISS_FTR_H
#define KISS_FTR_H

#include "kiss_fft.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct kiss_fftr_state *kiss_fftr_cfg;

kiss_fftr_cfg kiss_fftr_alloc(int nfft, int inverse_fft, void *mem, size_t *lenmem);
void kiss_fftr(kiss_fftr_cfg cfg, const kiss_fft_scalar *timedata, kiss_fft_cpx *freqdata);
void kiss_fftri(kiss_fftr_cfg cfg, const kiss_fft_cpx *freqdata, kiss_fft_scalar *timedata);
#define kiss_fftr_free KISS_FFT_FREE

#ifdef __cplusplus
}
#endif

#endif
