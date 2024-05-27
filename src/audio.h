/*
 * audio.h
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

#include <stdbool.h>
#include <stdint.h>

#include "ipnode.h"
#include "ax25_pad.h"

#define ONE_BUF_TIME 10

#define OCTYPE_PTT 0 // Push To Talk
#define OCTYPE_DCD 1 // Data Carrier Detect
#define OCTYPE_CON 2 // Connected Indicator
#define OCTYPE_SYN 3 // Sync Indicator
#define NUM_OCTYPES 4

#define ICTYPE_TXINH 0 // Transmit Inhibit
#define NUM_ICTYPES 1

#define MAX_GPIO_NAME_LEN 20

    struct ictrl_s
    {
        int in_gpio_num;
        int inh_invert;
        char in_gpio_name[MAX_GPIO_NAME_LEN];   // ASCII

    };

    struct octrl_s
    {
        int out_gpio_num;
        int ptt_invert;
        char out_gpio_name[MAX_GPIO_NAME_LEN]; /// ASCII
    };

    struct audio_s
    {
        int dwait;
        int slottime;
        int persist;
        int txdelay;
        int txtail;
        bool defined;
        bool fulldup;
        struct octrl_s octrl[NUM_OCTYPES];
        struct ictrl_s ictrl[NUM_ICTYPES];
        char adevice_in[80];                    // ASCII
        char adevice_out[80];                   // ASCII
        char mycall[AX25_MAX_ADDR_LEN];
    };

    int audio_open(struct audio_s *);
    int audio_get(void);
    void audio_put(uint8_t);
    void audio_flush(void);
    void audio_wait(void);
    void audio_close(void);

#ifdef __cplusplus
}
#endif
