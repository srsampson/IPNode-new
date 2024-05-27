/*
 * kiss_pt.c
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

#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <bsd/bsd.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "kiss_pt.h"
#include "transmit_queue.h"
#include "transmit_thread.h"

#define TMP_KISSTNC_SYMLINK "/tmp/kisstnc"

static pthread_t kiss_pterm_listen_tid;

static kiss_frame_t kiss_frame;

static int pt_master_fd = -1;  /* File descriptor for my end. */
static char pt_slave_name[32]; /* Pseudo terminal slave name  */

static void kiss_rec_byte(kiss_frame_t *, uint8_t, int);

static int kisspt_get()
{
    uint8_t chr;

    int n = 0;
    fd_set fd_in;
    fd_set fd_ex;

    while (n == 0)
    {
        FD_ZERO(&fd_in);
        FD_SET(pt_master_fd, &fd_in);

        FD_ZERO(&fd_ex);
        FD_SET(pt_master_fd, &fd_ex);

        int rc = select(pt_master_fd + 1, &fd_in, NULL, &fd_ex, NULL);

        if (rc == 0)
        {
            continue;
        }

        if (rc == -1 || (n = read(pt_master_fd, &chr, (size_t)1)) != 1)
        {
            fprintf(stderr, "kisspt_get: pt read Error receiving KISS message from pseudo terminal.  Closing %s\n", pt_slave_name);

            close(pt_master_fd);

            pt_master_fd = -1;
            unlink(TMP_KISSTNC_SYMLINK);
            pthread_exit(NULL);
            n = -1;
        }
    }

    return chr;
}

static void *kisspt_listen_thread(void *arg)
{
    while (1)
    {
        uint8_t chr = kisspt_get();  // calls select() so waits for data

        kiss_rec_byte(&kiss_frame, chr, -1);
    }

    return (void *)0;
}

static int kisspt_open_pt()
{
    int fd = posix_openpt(O_RDWR | O_NOCTTY);

    if (fd == -1)
    {
        fprintf(stderr, "kisspt_open_pt: Could not create pseudo terminal master\n");
        return -1;
    }

    char *pts = ptsname(fd);
    int grant = grantpt(fd);
    int unlock = unlockpt(fd);

    if ((grant == -1) || (unlock == -1) || (pts == NULL))
    {
        fprintf(stderr, "kisspt_open_pt: Could not create pseudo terminal\n");
        return -1;
    }

    strlcpy(pt_slave_name, pts, sizeof(pt_slave_name));

    struct termios ts;
    int e = tcgetattr(fd, &ts);

    if (e != 0)
    {
        fprintf(stderr, "kisspt_open_pt: pt tcgetattr Can't get pseudo terminal attributes, err=%d\n", e);
        return -1;
    }

    cfmakeraw(&ts);

    ts.c_cc[VMIN] = 1;  /* wait for at least one character */
    ts.c_cc[VTIME] = 0; /* no fancy timing. */

    e = tcsetattr(fd, TCSANOW, &ts);

    if (e != 0)
    {
        fprintf(stderr, "kisspt_open_pt: pt tcsetattr Can't set pseudo terminal attributes, err=%d\n", e);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    e = fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    if (e != 0)
    {
        fprintf(stderr, "kisspt_open_pt: Can't set pseudo terminal to nonblocking\n");
        return -1;
    }

    int pt_slave_fd = open(pt_slave_name, O_RDWR | O_NOCTTY);

    if (pt_slave_fd < 0)
    {
        fprintf(stderr, "kisspt_open_pt: Can't open pseudo terminal slave %s\n", pt_slave_name);
        return -1;
    }

    unlink(TMP_KISSTNC_SYMLINK);

    if (symlink(pt_slave_name, TMP_KISSTNC_SYMLINK) == 0)
    {
        printf("Created symlink %s -> %s\n", TMP_KISSTNC_SYMLINK, pt_slave_name);
    }
    else
    {
        fprintf(stderr, "kisspt_open_pt: Failed to create kiss symlink %s\n", TMP_KISSTNC_SYMLINK);
        return -1;
    }

    printf("Virtual KISS TNC is available on %s\n", pt_slave_name);

    return fd;
}

void kisspt_init()
{
    memset(&kiss_frame, 0, sizeof(kiss_frame));

    pt_master_fd = kisspt_open_pt();

    if (pt_master_fd != -1)
    {
        int e = pthread_create(&kiss_pterm_listen_tid, (pthread_attr_t *)NULL, kisspt_listen_thread, NULL);

        if (e != 0)
        {
            printf("Fatal: kisspt_init: Could not create kiss listening thread for pseudo terminal\n");
            exit(1);
        }
    }
}

static int kiss_encapsulate(uint8_t *in, int ilen, uint8_t *out)
{
    int olen;
    int j;

    olen = 0;
    out[olen++] = FEND;

    for (j = 0; j < ilen; j++)
    {
        uint8_t chr = in[j];

        if (chr == FEND)
        {
            out[olen++] = FESC;
            out[olen++] = TFEND;
        }
        else if (chr == FESC)
        {
            out[olen++] = FESC;
            out[olen++] = TFESC;
        }
        else
        {
            out[olen++] = chr;
        }
    }

    out[olen++] = FEND;

    return olen;
}

static int kiss_unwrap(uint8_t *in, int ilen, uint8_t *out)
{
    int olen = 0;
    bool escaped_mode = false;

    if (ilen < 2)
    {
        fprintf(stderr, "KISS message less than minimum length.\n");
        return 0;
    }

    if (in[ilen - 1] == FEND)
    {
        ilen--;
    }
    else
    {
        fprintf(stderr, "KISS frame should end with FEND.\n");
    }

    int j = (in[0] == FEND) ? 1 : 0;

    for (; j < ilen; j++)
    {
        uint8_t chr = in[j];

        if (chr == FEND)
        {
            fprintf(stderr, "KISS frame should not have FEND in the middle.\n");
        }

        if (escaped_mode == true)
        {

            if (chr == TFESC)
            {
                out[olen++] = FESC;
            }
            else if (chr == TFEND)
            {
                out[olen++] = FEND;
            }
            else
            {
                fprintf(stderr, "KISS protocol error.  Found 0x%02x after FESC.\n", chr);
            }

            escaped_mode = false;
        }
        else if (chr == FESC)
        {
            escaped_mode = true;
        }
        else
        {
            out[olen++] = chr;
        }
    }

    return olen;
}

static void kiss_process_msg(uint8_t *kiss_msg, int kiss_len, int client)
{
    int cmd = kiss_msg[0] & 0xf;
    packet_t pp;

    /* ignore all the other KISS bo-jive */

    switch (cmd)
    {
    case KISS_CMD_DATA_FRAME: /* 0 = Data Frame */

        pp = ax25_from_frame(kiss_msg + 1, kiss_len - 1);

        if (pp == NULL)
        {
            fprintf(stderr, "ERROR - Invalid KISS data frame.\n");
        }
        else
        {
            transmit_queue_append(TQ_PRIO_1_LO, pp);
        }
    }
}

static void kiss_rec_byte(kiss_frame_t *kf, uint8_t chr, int client)
{
    switch (kf->state)
    {

    case KS_SEARCHING: /* Searching for starting FEND. */
    default:
        if (chr == FEND)
        {
            kf->kiss_len = 0;
            kf->kiss_msg[kf->kiss_len++] = chr;
            kf->state = KS_COLLECTING;
        }
        break;

    case KS_COLLECTING: /* Frame collection in progress. */
        if (chr == FEND)
        {
            uint8_t unwrapped[AX25_MAX_PACKET_LEN];

            /* End of frame. */

            if (kf->kiss_len == 0)
            {
                /* Empty frame.  Starting a new one. */
                kf->kiss_msg[kf->kiss_len++] = chr;
                return;
            }

            if (kf->kiss_len == 1 && kf->kiss_msg[0] == FEND)
            {
                /* Empty frame.  Just go on collecting. */
                return;
            }

            kf->kiss_msg[kf->kiss_len++] = chr;

            int ulen = kiss_unwrap(kf->kiss_msg, kf->kiss_len, unwrapped);

            kiss_process_msg(unwrapped, ulen, client);

            kf->state = KS_SEARCHING;
            return;
        }

        if (kf->kiss_len < MAX_KISS_LEN)
        {
            kf->kiss_msg[kf->kiss_len++] = chr;
        }
        else
        {
            fprintf(stderr, "KISS message exceeded maximum length.\n");
        }
    }
}

void kisspt_send_rec_packet(int kiss_cmd, uint8_t *fbuf, int flen)
{
    uint8_t kiss_buff[2 * AX25_MAX_PACKET_LEN + 2];
    int kiss_len;

    if (pt_master_fd == -1)
    {
        return;
    }

    if (flen < 0)
    {
        flen = strlen((char *)fbuf);

        strlcpy((char *)kiss_buff, (char *)fbuf, sizeof(kiss_buff));
        kiss_len = strlen((char *)kiss_buff);
    }
    else
    {
        uint8_t stemp[AX25_MAX_PACKET_LEN + 1];

        if (flen > (int)(sizeof(stemp)) - 1)
        {
            fprintf(stderr, "kisspt_send_rec_packet: Pseudo Terminal KISS buffer too small.  Truncated.\n");
            flen = (int)(sizeof(stemp)) - 1;
        }

        stemp[0] = kiss_cmd & 0x0F;
        memcpy(stemp + 1, fbuf, flen);

        kiss_len = kiss_encapsulate(stemp, flen + 1, kiss_buff);
    }

    int err = write(pt_master_fd, kiss_buff, (size_t)kiss_len);

    if (err == -1 && errno == EWOULDBLOCK)
    {
        fprintf(stderr, "kisspt_send_rec_packet: Discarding KISS Send message because no listener\n");
    }
    else if (err != kiss_len)
    {
        fprintf(stderr, "kisspt_send_rec_packet: KISS pseudo terminal write error: fd=%d, len=%d, write returned %d, errno = %d\n",
                pt_master_fd, kiss_len, err, errno);
    }
}
