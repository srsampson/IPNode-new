/*
 * receive_thread.h
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
#include <stdbool.h>

#include "audio.h"
#include "ax25_pad.h"

#define EOF_COST_VALUE 0.99

    struct demodulator_state_s
    {
        float quick_attack;
        float sluggish_decay;
        float alevel_rec_peak;
        float alevel_rec_valley;
    };

    void rx_init(struct audio_s *);
    int demod_get_audio_level(void);
    bool get_dcd_detect(void);
    void set_dcd_detect(bool);
    float get_offset_freq(void);

#ifdef __cplusplus
}
#endif
