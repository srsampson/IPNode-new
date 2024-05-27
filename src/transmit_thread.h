/*
 * transmit_thread.h
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

#include <complex.h>
#include <stdint.h>

#include "audio.h"

    void tx_init(struct audio_s *);
    void tx_frame_bits(int, uint8_t *, int);

#ifdef __cplusplus
}
#endif
