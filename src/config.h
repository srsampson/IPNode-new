/*
 * config.h
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

#include "audio.h"

#define MAXCMDLEN 1200

#define DEFAULT_ADEVICE "default"

#define DEFAULT_DWAIT 0
#define DEFAULT_SLOTTIME 10
#define DEFAULT_PERSIST 63
#define DEFAULT_TXDELAY 10
#define DEFAULT_TXTAIL 10
#define DEFAULT_FULLDUP 0

    struct misc_config_s
    {
        int frack;    /* Number of seconds to wait for ack to transmission. */
        int retry;    /* Number of times to retry before giving up. */
        int paclen;   /* Max number of bytes in information part of frame. */
        int maxframe; /* Max frames to send before ACK.  mod 8 "Window" size. */
    };

    void config_init(char *, struct audio_s *, struct misc_config_s *);

#ifdef __cplusplus
}
#endif
