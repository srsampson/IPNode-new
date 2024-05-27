/*
 * receive_queue.h
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

#include "ax25_pad.h"
#include "audio.h"

#define TXDATA_MAGIC 0x09110911

    typedef struct cdata_s
    {
        struct cdata_s *next;
        int magic;
        int pid;
        int size;
        int len;
        uint8_t data[];
    } cdata_t;

    /* Types of things that can be in queue. */

    typedef enum rxq_type_e
    {
        RXQ_REC_FRAME,
        RXQ_CHANNEL_BUSY,
        RXQ_SEIZE_CONFIRM
    } rxq_type_t;

    typedef struct rx_queue_item_s
    {
        struct rx_queue_item_s *nextp;
        cdata_t *txdata;
        packet_t pp;
        rxq_type_t type;
        int client;
        int activity;
        int status;
        char addrs[AX25_ADDRS][AX25_MAX_ADDR_LEN];
    } rxq_item_t;

    void rx_queue_init(void);
    void rx_queue_rec_frame(packet_t);
    void rx_queue_channel_busy(int, int);
    void rx_queue_seize_confirm(void);
    int rx_queue_wait_while_empty(double);
    struct rx_queue_item_s *rx_queue_remove(void);
    void rx_queue_delete(struct rx_queue_item_s *);
    cdata_t *cdata_new(int, uint8_t *, int);
    void cdata_delete(cdata_t *);

#ifdef __cplusplus
}
#endif
