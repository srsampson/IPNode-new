/*
 * transmit_thread.c
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
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <stddef.h>
#include <complex.h>

#include "ipnode.h"
#include "ax25_link.h"
#include "ax25_pad.h"
#include "audio.h"
#include "il2p.h"
#include "transmit_queue.h"
#include "transmit_thread.h"
#include "ptt.h"
#include "receive_queue.h"
#include "receive_thread.h"
#include "rrc_fir.h"
#include "constellation.h"

extern bool node_shutdown;

static int tx_baud;
static int tx_slottime;
static int tx_persist;
static int tx_txdelay;
static int tx_txtail;
static bool tx_fulldup;

#define WAIT_TIMEOUT_MS (60 * 1000)
#define WAIT_CHECK_EVERY_MS 10
#define BITS_TO_MS(b) (((b)*875) / tx_baud)
#define MS_TO_BITS(ms) (((ms)*tx_baud) / 875) // 100 ms == 137 bits

static void *tx_thread(void *);
static bool wait_for_clear_channel(int, int, bool);
static void tx_frames(int, packet_t);
static int send_one_frame(packet_t);
static void put_symbols(complex float[], int);

static pthread_t tx_tid;
static pthread_mutex_t audio_out_dev_mutex;
static struct audio_s *save_audio_config_p;

static complex float tx_filter[NTAPS];

static complex float m_txPhase;
static complex float m_txRect;
static complex float *m_qpsk;

static void *tx_thread(void *arg)
{
    while (node_shutdown == false)
    {

        transmit_queue_wait_while_empty();

        while (transmit_queue_peek(TQ_PRIO_0_HI) != NULL || transmit_queue_peek(TQ_PRIO_1_LO) != NULL)
        {
            bool ok = wait_for_clear_channel(tx_slottime, tx_persist, tx_fulldup);

            int prio = TQ_PRIO_1_LO;
            packet_t pp = transmit_queue_remove(TQ_PRIO_0_HI);

            if (pp != NULL)
            {
                prio = TQ_PRIO_0_HI;
            }
            else
            {
                pp = transmit_queue_remove(TQ_PRIO_1_LO);
            }

            if (pp != NULL)
            {
                if (ok == true)
                {
                    tx_frames(prio, pp);
                    il2p_mutex_unlock(&audio_out_dev_mutex);
                }
                else
                {
                    ax25_delete(pp);
                }
            }
        }
    }

    return 0;
}

void tx_init(struct audio_s *p_modem)
{
    save_audio_config_p = p_modem;

    tx_slottime = p_modem->slottime;
    tx_persist = p_modem->persist;
    tx_txdelay = p_modem->txdelay;
    tx_txtail = p_modem->txtail;
    tx_fulldup = p_modem->fulldup;
    tx_baud = 1200;

    transmit_queue_init();

    il2p_mutex_init(&audio_out_dev_mutex);

    int e = pthread_create(&tx_tid, NULL, tx_thread, (void *)NULL);

    if (e != 0)
    {
        fprintf(stderr, "tx_init: Could not create transmitter thread for modem\n");
        exit(1);
    }

    // Passband Center Frequency is 1000 Hz

    m_txRect = cmplx((TAU * CENTER) / FS);
    m_txPhase = cmplx(0.0f);

    m_qpsk = getQPSKConstellation();
}

#ifdef NOT_USED
#define CLIP_THRESHOLD 1.0f     // you will have to compute looking at output

/*
 * Hilbert Clipper
 *
 * Used to improve peak-to-average power ratio (PAPR)
 */
static void clip(complex float tx[], int size) {
    for (int i = 0; i < size; i++) {
        complex float sam = tx[i];
        float mag = cabsf(sam);         // compute complex magnitude
    
        if (mag > CLIP_THRESHOLD) {
            sam *= (CLIP_THRESHOLD / mag);
        }

        tx[i] = sam;
    }
}
#endif

/*
 * Modulate and upsample symbols
 * Sending them to the soundcard
 */
static void put_symbols(complex float symbols[], int symbolsCount)
{
    int outputSize = CYCLES * symbolsCount; // upsample 1200 to 9600

    complex float signal[outputSize]; // transmit signal

    /*
     * Use zero-insertion to change the
     * sample rate from 1200 to 9600.
     */
    for (int i = 0; i < symbolsCount; i++)
    {
        int index = (i * CYCLES); // compute once

        signal[index] = symbols[i];

        for (int j = 1; j < CYCLES; j++)
        {
            signal[index + j] = CMPLXF(0.0f, 0.0f);
        }
    }

    /*
     * Root Cosine Filter pulse baseband
     */
    rrc_fir(tx_filter, signal, outputSize);

    /*
     * Shift filtered Baseband to Passband
     */
    for (int i = 0; i < outputSize; i++)
    {
        m_txPhase *= m_txRect;
        signal[i] *= (m_txPhase * 32768.0f); // Factor PCM amplitude
    }

    /*
     * Store PCM audio (real part) in output buffer
     */
    for (int i = 0; i < outputSize; i++)
    {
        short pcm = (short)(crealf(signal[i])); // (* 16k above)
        audio_put(pcm & 0xff);                  // little-endian
        audio_put((pcm >> 8) & 0xff);
    }
}

/*
 * Transmit octets
 *
 * These are pulses to be filtered and modulated
 */
void tx_frame_bits(int mode, uint8_t tx_bits[], int num_bits)
{
    int symbol_count = 0;
    int bit_count = 0;
    int save_bit;

    if (mode == Mode_QPSK)
    {
        complex float tx_symbols[num_bits / 2]; // 2-Bits per symbol

        for (int i = 0; i < num_bits; i++)
        {
            if (bit_count == 0) // wait for 2 bits
            {
                save_bit = tx_bits[i];
                bit_count++;

                continue;
            }

            uint8_t dibit = ((save_bit << 1) | tx_bits[i]) & 0x3;

            tx_symbols[symbol_count++] = getQPSKQuadrant(dibit);

            save_bit = 0; // reset for next bits
            bit_count = 0;
        }

        put_symbols(tx_symbols, symbol_count);
    }
    else if (mode == Mode_BPSK) // Mode_BPSK
    {
        complex float tx_symbols[num_bits]; // 1-Bit per symbol

        for (int i = 0; i < num_bits; i++)
        {
            tx_symbols[symbol_count++] = getQPSKQuadrant((tx_bits[i] == 0) ? 0 : 3);
        }

        put_symbols(tx_symbols, symbol_count);
    }
    else if (mode == Mode_SYNC) // Send FLAGS at 300 baud
    {
        complex float tx_symbols[num_bits]; // 1-Bit per symbol

        for (int i = 0; i < num_bits; i++)
        {
            tx_symbols[symbol_count++] = getQPSKQuadrant((tx_bits[i] == 0) ? 0 : 3) * .75f; // 75% amplitude
        }

        put_symbols(tx_symbols, symbol_count);
    }
}

/*
 * Check to see if we are receiving valid data
 * (DCD active) or if we are full duplex and
 * DCD doesn't matter
 */
static bool wait_for_clear_channel(int slottime, int persist, bool fulldup)
{
    int n = 0;

    if (fulldup == false)
    {

    start_over_again:

        while (get_dcd_detect() == true)
        {
            SLEEP_MS(WAIT_CHECK_EVERY_MS);

            n++;

            if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS))
            {
                return false;
            }
        }

        if (save_audio_config_p->dwait > 0)
        {
            SLEEP_MS(save_audio_config_p->dwait * 10);
        }

        if (get_dcd_detect() == true)
        {
            goto start_over_again;
        }

        while (transmit_queue_peek(TQ_PRIO_0_HI) == NULL)
        {
            SLEEP_MS(slottime * 10);

            if (get_dcd_detect() == true)
            {
                goto start_over_again;
            }

            int r = rand() & 0xff;

            if (r <= persist)
            {
                break;
            }
        }
    }

    while (!il2p_mutex_try_lock(&audio_out_dev_mutex))
    {
        SLEEP_MS(WAIT_CHECK_EVERY_MS);

        n++;

        if (n > (WAIT_TIMEOUT_MS / WAIT_CHECK_EVERY_MS))
        {
            return false;
        }
    }

    return true;
}

static int send_one_frame(packet_t pp)
{
    if (ax25_is_null_frame(pp))
    {
        rx_queue_seize_confirm();

        SLEEP_MS(10);

        return 0;
    }

    return il2p_send_frame(pp);
}

static void tx_frames(int prio, packet_t pp)
{
    int numframe = 0;
    int num_bits = 0;
 
    double time_ptt = dtime_now();

    ptt_set(OCTYPE_PTT, true);

    rx_queue_seize_confirm();

    // Find out how many bits we need at 1200
    int flags = MS_TO_BITS(tx_txdelay * 10);

    // divide bits to find octets
    il2p_send_idle(flags / 8); // each flag is one octet
    num_bits += flags;

    /*
     * Give other threads some time
     */
    SLEEP_MS(10);

    /*
     * Send the frame
     */
    int nb = send_one_frame(pp);

    if (nb > 0)
    {
        num_bits += nb;
        numframe++;
    }

    ax25_delete(pp);

    /*
     * Now while we are here, send any other
     * waiting frames.
     */
    bool done = false;

    while (numframe < 256 && (done == false))
    {
        prio = TQ_PRIO_1_LO;
        pp = transmit_queue_peek(TQ_PRIO_0_HI);

        if (pp != NULL)
        {
            prio = TQ_PRIO_0_HI;
        }
        else
        {
            pp = transmit_queue_peek(TQ_PRIO_1_LO);
        }

        if (pp != NULL)
        {
            pp = transmit_queue_remove(prio);

            nb = send_one_frame(pp);

            if (nb > 0)
            {
                num_bits += nb;
                numframe++;
            }

            ax25_delete(pp);
        }
        else
        {
            done = true;
        }
    }

    /*
     * Now send the tx_tail
     */
    flags = MS_TO_BITS(tx_txtail * 10);

    il2p_send_idle(flags / 8);
    num_bits += flags;

    /*
     * Get the souncard pushing
     */
    audio_flush();
    audio_wait();

    int duration = BITS_TO_MS(num_bits);

    double time_now = dtime_now();

    int already = (int)((time_now - time_ptt) * 1000.0);
    int wait_more = (duration - already);

    if (wait_more > 0)
    {
        SLEEP_MS(wait_more);
    }

    ptt_set(OCTYPE_PTT, false);
}
