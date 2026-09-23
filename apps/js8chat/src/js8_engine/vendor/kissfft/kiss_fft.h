/*
 *  Copyright (c) 2003-2010, Mark Borgerding. All rights reserved.
 *  This file is part of KISS FFT - https://github.com/mborgerding/kissfft
 *
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef KISS_FFT_H
#define KISS_FFT_H

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Private float-only, caller-workspace-only JS8 copy. */
#define kiss_fft_scalar float
#define kiss_fft_alloc js8_kiss_fft_alloc
#define kiss_fft js8_kiss_fft
#define kiss_fft_stride js8_kiss_fft_stride
#define kiss_fft_cleanup js8_kiss_fft_cleanup
#define kiss_fft_next_fast_size js8_kiss_fft_next_fast_size
#define kiss_fftr_alloc js8_kiss_fftr_alloc
#define kiss_fftr js8_kiss_fftr

typedef struct {
    kiss_fft_scalar r;
    kiss_fft_scalar i;
} kiss_fft_cpx;

typedef struct kiss_fft_state* kiss_fft_cfg;

kiss_fft_cfg kiss_fft_alloc(int nfft, int inverse_fft, void *mem, size_t *lenmem);
void kiss_fft(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout);
void kiss_fft_stride(kiss_fft_cfg cfg, const kiss_fft_cpx *fin, kiss_fft_cpx *fout, int fin_stride);
void kiss_fft_cleanup(void);
int kiss_fft_next_fast_size(int n);
#define kiss_fftr_next_fast_size_real(n) (kiss_fft_next_fast_size(((n)+1)>>1)<<1)

#ifdef __cplusplus
}
#endif

#endif
