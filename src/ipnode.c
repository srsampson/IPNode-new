/*
 * ipnode.c
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
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <bsd/bsd.h>
#include <bsd/string.h>
#include <sys/soundcard.h>

#include "ipnode.h"
#include "audio.h"
#include "config.h"
#include "receive_queue.h"
#include "kiss_pt.h"
#include "transmit_thread.h"
#include "ptt.h"
#include "receive_thread.h"
#include "ax25_link.h"
#include "il2p.h"
#include "costas_loop.h"
#include "constellation.h"
#include "rrc_fir.h"
#include "ted.h"

bool node_shutdown;

#define IS_DIR_SEPARATOR(c) ((c) == '/')

static struct audio_s audio_config;
static struct misc_config_s misc_config;
static char *progname;

/* Process control-C and window close events. */

static void cleanup(int x)
{
    node_shutdown = true; // kill tx/rx threads

    ptt_term();
    audio_close();

    SLEEP_SEC(1);
    exit(0);
}

static void app_process_rec_packet(packet_t pp)
{
    uint8_t fbuf[AX25_MAX_PACKET_LEN];

    int flen = ax25_pack(pp, fbuf);

    kisspt_send_rec_packet(KISS_CMD_DATA_FRAME, fbuf, flen); // KISS pseudo terminal
}

/*
 * Called from main after config and setup
 */
static void rx_process()
{
    struct rx_queue_item_s *pitem;

    while (1)
    {
        if (rx_queue_wait_while_empty(ax25_link_get_next_timer_expiry()) == true)
        {
            dl_timer_expiry();
        }
        else
        {
            pitem = rx_queue_remove();

            if (pitem != NULL)
            {
                switch (pitem->type)
                {
                case RXQ_REC_FRAME:
                    app_process_rec_packet(pitem->pp);
                    lm_data_indication(pitem);
                    break;

                case RXQ_CHANNEL_BUSY:
                    lm_channel_busy(pitem);
                    break;

                case RXQ_SEIZE_CONFIRM:
                    lm_seize_confirm(pitem);
                    break;
                }

                rx_queue_delete(pitem);
            }
        }
    }
}

int main(int argc, char *argv[])
{
    char config_file[100];
    char input_file[80];

    if (getuid() == 0 || geteuid() == 0)
    {
        printf("Do not run as root\n");
        exit(1);
    }

    char *pn = argv[0] + strlen(argv[0]);

    while (pn != argv[0] && !IS_DIR_SEPARATOR(pn[-1]))
        --pn;

    progname = pn;

    // default name
    strlcpy(config_file, "ipnode.conf", sizeof(config_file));

    config_init(config_file, &audio_config, &misc_config);

    strlcpy(input_file, "", sizeof(input_file));

    signal(SIGINT, cleanup);

    /*
     * Open the audio source
     */
    int err = audio_open(&audio_config);

    if (err < 0)
    {
        fprintf(stderr, "Fatal: No audio device found %d\n", err);
        SLEEP_SEC(5);
        exit(1);
    }

    createQPSKConstellation();

    /*
     * Create an RRC filter using the
     * Sample Rate, Baud, and Alpha
     */
    rrc_make(FS, RS, .35f);

    /*
     * Create a costas loop
     *
     * All terms are radians per sample.
     *
     * The loop bandwidth determins the lock range
     * and should be set around TAU/100 to TAU/200
     */
    create_control_loop((TAU / 180.0f), -1.0f, 1.0f);

    node_shutdown = false;

    rx_queue_init();
    ax25_link_init(&misc_config);
    il2p_init();
    // ptt_init(&audio_config);       ///////////// TODO disabled for debugging
    tx_init(&audio_config);
    rx_init(&audio_config);

    create_timing_error_detector();

    kisspt_init();                    // kiss pseudo-terminal

    // Run as a daemon process forever

    rx_process();

    exit(EXIT_SUCCESS);
}
