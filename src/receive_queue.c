/*
 * receive_queue.c
 *
 * IP Node Project
 *
 * Based on the Dire Wolf program
 * Copyright (C) 2011-2021 John Langner
 *
 * Fork by Steve Sampson, K5OKC, May 2024
 *
 * Changes suggested by Brent Petit Oct 29, 2023
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "ipnode.h"
#include "ax25_pad.h"
#include "audio.h"
#include "receive_queue.h"

static struct rx_queue_item_s *queue_head = NULL;
static struct rx_queue_item_s *queue_tail = NULL;
static int queue_length = 0;

static pthread_mutex_t rx_queue_mutex;
static pthread_cond_t wake_up_cond;

static volatile bool recv_thread_is_waiting = false;

static volatile int s_new_count = 0;
static volatile int s_delete_count = 0;
static volatile int s_cdata_new_count = 0;
static volatile int s_cdata_delete_count = 0;

void rx_queue_init()
{
    queue_head = queue_tail = NULL;
    queue_length = 0;

    int err = pthread_mutex_init(&rx_queue_mutex, NULL);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_init: pthread_mutex_init err=%d", err);
        exit(1);
    }

    err = pthread_cond_init(&wake_up_cond, NULL);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_init: pthread_cond_init err=%d", err);
        exit(1);
    }

    recv_thread_is_waiting = false;
}

static void append_to_rx_queue(struct rx_queue_item_s *pnew)
{
    pnew->nextp = NULL;

    int err = pthread_mutex_lock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue append_to_rx_queue: pthread_mutex_lock err=%d", err);
        exit(1);
    }

    if (queue_head == NULL)
    {
        queue_head = queue_tail = pnew;
        queue_length = 1;
    }
    else
    {
        queue_tail->nextp = pnew;
        queue_tail = pnew;
        queue_length++;
    }

    if (queue_length > 15)
    {
        fprintf(stderr, "rx_queue append_to_rx_queue: receive queue is out of control. length=%d.\n", queue_length);
    }

    if (recv_thread_is_waiting == true)
    {
        err = pthread_cond_signal(&wake_up_cond);

        if (err != 0)
        {
            fprintf(stderr, "rx_queue append_to_rx_queue: pthread_cond_signal err=%d", err);
            exit(1);
        }
    }

    err = pthread_mutex_unlock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue append_to_rx_queue: pthread_mutex_unlock err=%d", err);
        exit(1);
    }
}

/*
 * Called from il2p_rec upon IL2P_DECODE
 */
void rx_queue_rec_frame(packet_t pp)
{
    struct rx_queue_item_s *pnew = (struct rx_queue_item_s *)calloc(1, sizeof(struct rx_queue_item_s));

    s_new_count++;

    // TODO where does 50 come from?

    if (s_new_count > s_delete_count + 50)
    {
        fprintf(stderr, "rx_queue_rec_frame: queue memory leak, new=%d, delete=%d\n", s_new_count, s_delete_count);
    }

    pnew->nextp = NULL;
    pnew->type = RXQ_REC_FRAME;
    pnew->pp = pp;

    append_to_rx_queue(pnew);
}

/*
 * Called from ptt
 */
void rx_queue_channel_busy(int activity, int status)
{
    if (activity == OCTYPE_PTT || activity == OCTYPE_DCD)
    {
        struct rx_queue_item_s *pnew = (struct rx_queue_item_s *)calloc(1, sizeof(struct rx_queue_item_s));

        if (pnew == NULL)
        {
            fprintf(stderr, "rx_queue_channel_busy: Out of memory.\n");
            exit(1);
        }

        s_new_count++;

        pnew->type = RXQ_CHANNEL_BUSY;
        pnew->activity = activity;
        pnew->status = status;

        append_to_rx_queue(pnew);
    }
}

/*
 * Called from tx
 */
void rx_queue_seize_confirm()
{
    struct rx_queue_item_s *pnew = (struct rx_queue_item_s *)calloc(1, sizeof(struct rx_queue_item_s));

    if (pnew == NULL)
    {
        fprintf(stderr, "rx_queue_seize_confirm: Out of memory.\n");
        exit(1);
    }

    s_new_count++;

    pnew->type = RXQ_SEIZE_CONFIRM;

    append_to_rx_queue(pnew);
}

bool rx_queue_wait_while_empty(double timeout)
{
    bool timed_out_result = false;

    int err = pthread_mutex_lock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_wait_while_empty: pthread_mutex_lock wu err=%d", err);
        exit(1);
    }

    if (queue_head == NULL)
    {
        recv_thread_is_waiting = true;

        if (timeout != 0.0)
        {
            struct timespec abstime;

            abstime.tv_sec = (time_t)(long)timeout;
            abstime.tv_nsec = (long)((timeout - (long)abstime.tv_sec) * 1000000000.0);

            err = pthread_cond_timedwait(&wake_up_cond, &rx_queue_mutex, &abstime);

            if (err == ETIMEDOUT)
            {
                timed_out_result = true;
            }
        }
        else
        {
            err = pthread_cond_wait(&wake_up_cond, &rx_queue_mutex);
        }

        recv_thread_is_waiting = false;
    }

    err = pthread_mutex_unlock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_wait_while_empty: pthread_mutex_unlock wu err=%d", err);
        exit(1);
    }

    return timed_out_result;
}

struct rx_queue_item_s *rx_queue_remove()
{
    struct rx_queue_item_s *result = NULL;

    int err = pthread_mutex_lock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_remove: pthread_mutex_lock err=%d", err);
        exit(1);
    }

    if (queue_head != NULL)
    {
        result = queue_head;
        queue_head = queue_head->nextp;
        queue_length--;

        if (queue_head == NULL)
        {
            queue_tail = NULL;
        }
    }

    err = pthread_mutex_unlock(&rx_queue_mutex);

    if (err != 0)
    {
        fprintf(stderr, "rx_queue_remove: pthread_mutex_unlock err=%d", err);
        exit(1);
    }

    return result;
}

void rx_queue_delete(struct rx_queue_item_s *pitem)
{
    if (pitem == NULL)
    {
        fprintf(stderr, "rx_queue_delete() given NULL pointer.\n");
        return;
    }

    s_delete_count++;

    if (pitem->pp != NULL)
    {
        ax25_delete(pitem->pp);
        pitem->pp = NULL;
    }

    if (pitem->txdata != NULL)
    {
        cdata_delete(pitem->txdata);
        pitem->txdata = NULL;
    }

    free(pitem);
}

/*
 * Connected data functions for ax25_link.c
 */
cdata_t *cdata_new(int pid, uint8_t *data, int len)
{
    int size = (len + 127) & ~0x7f;

    cdata_t *cdata = calloc(1, sizeof(cdata_t) + size);

    cdata->magic = TXDATA_MAGIC;
    cdata->next = NULL;
    cdata->pid = pid;
    cdata->size = size;
    cdata->len = len;

    s_cdata_new_count++;

    if (data != NULL)
    {
        memcpy(cdata->data, data, len);
    }

    return cdata;
}

void cdata_delete(cdata_t *cdata)
{
    if (cdata == NULL)
    {
        return;
    }

    if (cdata->magic != TXDATA_MAGIC)
    {
        fprintf(stderr, "cdata_delete: connected data block corrupt\n");
        return;
    }

    s_cdata_delete_count++;

    free(cdata);
}
