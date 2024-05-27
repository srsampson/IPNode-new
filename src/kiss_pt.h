/*
 * kiss_pt.h
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

#include <stdint.h>

#include "audio.h"
#include "ax25_pad.h"
#include "config.h"

#define KISS_CMD_DATA_FRAME 0

#define FEND 0xC0
#define FESC 0xDB
#define TFEND 0xDC
#define TFESC 0xDD

    enum kiss_state_e
    {
        KS_SEARCHING,
        KS_COLLECTING
    };

#define MAX_KISS_LEN 2048

    typedef struct kiss_frame_s
    {
        enum kiss_state_e state;
        int kiss_len;
        uint8_t kiss_msg[MAX_KISS_LEN];
    } kiss_frame_t;

    void kisspt_init(void);
    void kisspt_send_rec_packet(int, uint8_t *, int);

#ifdef __cplusplus
}
#endif
