/*
 * receive_thread.c
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

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <complex.h>

#include "ipnode.h"
#include "audio.h"
#include "receive_thread.h"
#include "il2p.h"
#include "costas_loop.h"
#include "rrc_fir.h"
#include "ptt.h"
#include "constellation.h"
#include "ted.h"
#include "ax25_link.h"

extern bool node_shutdown;

static pthread_t rx_tid;

// Globals

static struct demodulator_state_s demod_state;
static struct demodulator_state_s *D = &demod_state;

static complex float rx_filter[NTAPS];
static complex float m_rxPhase;
static complex float m_rxRect;
static complex float recvBlock[8]; // 8 CYCLES per symbol

static float m_offset_freq;

static bool dcdDetect;

static float cnormf(complex float val)
{
    float realf = crealf(val);
    float imagf = cimagf(val);

    return (realf * realf) + (imagf * imagf);
}

/*
 * QPSK Receive function
 *
 * Process a vector of real samples at 9600 rate
 * Remove any frequency and timing offsets
 *
 * Vector samples are converted to baseband
 * from the passband center frequency of 1 kHz.
 *
 * Results in one 1200 Baud decoded symbol
 * which the dibits are sent on to the L2
 * protocol decoder.
 */
static void processSymbols(float csamples[])
{
    uint8_t diBits;

    /*
     * Convert 9600 rate samples to baseband.
     */
    for (int i = 0; i < CYCLES; i++)
    {
        m_rxPhase *= m_rxRect;

        recvBlock[i] = m_rxPhase * csamples[i];
    }

    rrc_fir(rx_filter, recvBlock, CYCLES);

    /*
     * Decimate by 4 for TED calculation (two samples per symbol)
     */
    for (int i = 0; i < CYCLES; i += 4)
    {
        ted_input(&recvBlock[i]);
    }

    complex float decision = getMiddleSample(); // use middle TED sample

    /*
     * Update audio levels (not really used yet)
     */
    float fsam = cnormf(decision);

    if (fsam >= D->alevel_rec_peak)
    {
        D->alevel_rec_peak = fsam * D->quick_attack + D->alevel_rec_peak *
         (1.0f - D->quick_attack);
    }
    else
    {
        D->alevel_rec_peak = fsam * D->sluggish_decay + D->alevel_rec_peak *
         (1.0f - D->sluggish_decay);
    }

    if (fsam <= D->alevel_rec_valley)
    {
        D->alevel_rec_valley = fsam * D->quick_attack + D->alevel_rec_valley *
         (1.0f - D->quick_attack);
    }
    else
    {
        D->alevel_rec_valley = fsam * D->sluggish_decay + D->alevel_rec_valley *
         (1.0f - D->sluggish_decay);
    }

    complex float costasSymbol = decision * cmplxconj(get_phase());

    float phase_error = phase_detector(costasSymbol);

    advance_loop(phase_error);
    phase_wrap();
    frequency_limit();

    /*
     * If the phase error isn't near +/- Pi/4 radians
     * it probably can't be decoded properly.
     * 
     * TODO Pi/4 radians is a WAG
     */
    if (fabsf(phase_error) <= (M_PI / 4.0f))
    {
        dcdDetect = true;
        
        diBits = qpskToDiBit(costasSymbol);
        
        /*
         * Add to the output stream MSB first
         */
        il2p_rec_bit((diBits >> 1) & 0x1);
        il2p_rec_bit(diBits & 0x1);
    }

    /*
     * Okay, toggle DCD back to off
     */
    dcdDetect = false;

    /*
     * Detected frequency error (for external display maybe)
     */
    m_offset_freq = (get_frequency() * RS / TAU); // convert radians to Hz at symbol rate
}

/*
 * Called by receive thread
 *
 * This will fill the csamples vector
 * with a CYCLES (8) worth of real values
 * which is at the 9600 sample rate
 */
static bool demod_get_samples(float csamples[])
{
    for (int i = 0; i < CYCLES; i++)
    {
        int lsb = audio_get(); // get real bytes

        if (lsb < 0)
            return false;

        int msb = audio_get();

        if (msb < 0)
            return false;

        signed short pcm = ((msb << 8) | lsb) & 0xffff;

        csamples[i] = (float) pcm / 32768.0f;
    }

    return true;
}

/*
 * Probably need to processSymbols only
 * after the audio breaks some AGC threshold.
 * TODO
 */
static void *rx_adev_thread(void *arg)
{
    float csamples[CYCLES];

    while (node_shutdown == false)
    {
        /*
         * Get a vector of CYCLES (8)
         * complex IQ values at 9600 rate
         */
        if (demod_get_samples(csamples) == true)
        {
            /*
             * If successful, process the vector
             * This will toggle DCD based on Costas Loop 
             */
            processSymbols(csamples);
        }
    }

    fprintf(stderr, "\nShutdown: Terminating after audio input closed.\n");
    exit(1);
}

void rx_init(struct audio_s *pa)
{
    if (pa->defined == true)
    {
        int e = pthread_create(&rx_tid, NULL, rx_adev_thread, 0);

        if (e != 0)
        {
            fprintf(stderr, "rx_init: Could not create receive audio thread\n");
            exit(1);
        }
    }
    else
    {
        fprintf(stderr, "rx_init: %s(): No audio device defined\n", __func__);
        exit(1);
    }

    dcdDetect = false;

    m_rxRect = cmplxconj((TAU * CENTER) / FS);
    m_rxPhase = cmplx(0.0f);

    memset(D, 0, sizeof(struct demodulator_state_s));

    D->quick_attack = 0.080f * 0.2f;
    D->sluggish_decay = 0.00012f * 0.2f;
}

bool get_dcd_detect()
{
    return dcdDetect;
}

void set_dcd_detect(bool val)
{
    dcdDetect = val;
}

/*
 * This is not fully implemented yet
 */
int demod_get_audio_level()
{
    // Take half of peak-to-peak for received audio level.

    return (int)((D->alevel_rec_peak - D->alevel_rec_valley) * 50.0f + 0.5f);
}

float get_offset_freq()
{
    return m_offset_freq;
}
