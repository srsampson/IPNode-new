/*
 * ipnode.h
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * Fork by Steve Sampson, K5OKC, May 2024
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#define G_UNKNOWN -999999

#include <unistd.h>
#include <pthread.h>

#include "ax25_pad.h" // packet_t

#define SLEEP_SEC(n) sleep(n)
#define SLEEP_MS(n) usleep((n)*1000)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TAU (2.0 * M_PI)
#define ROTATE45 (M_PI / 4.0)

#define CENTER 1000.0
#define FS 9600.0
#define RS 1200.0
#define CYCLES (int)(FS/RS)

/*
 * Idle Flag
 */
#define FLAG 0b00000000     // 00001111 1200 / 4 = 300 baud BPSK

#define Mode_BPSK 0
#define Mode_QPSK 1
#define Mode_SYNC 2

/*
 * This method is much faster than using cexp()
 * float_value - must be a float
 */
#define cmplx(float_value) (cosf(float_value) + sinf(float_value) * I)
#define cmplxconj(float_value) (cosf(float_value) + sinf(float_value) * -I)

#define il2p_mutex_init(x) pthread_mutex_init(x, NULL)

#ifdef __cplusplus
}
#endif
