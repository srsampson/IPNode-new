/*
 * il2p.h
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

/*
 * The Sync Word is 3-Bytes or 24-bits...
 */
#define IL2P_SYNC_WORD_SIZE 3

/*
 * ...and contains the following value:
 */
#define IL2P_SYNC_WORD 0xF15E48

#define IL2P_HEADER_SIZE 13
#define IL2P_HEADER_PARITY 2

#define IL2P_MAX_PAYLOAD_SIZE 1023
#define IL2P_MAX_PAYLOAD_BLOCKS 5
#define IL2P_MAX_PARITY_SYMBOLS 16
#define IL2P_MAX_ENCODED_PAYLOAD_SIZE (IL2P_MAX_PAYLOAD_SIZE + IL2P_MAX_PAYLOAD_BLOCKS * IL2P_MAX_PARITY_SYMBOLS)

#define IL2P_MAX_PACKET_SIZE (IL2P_SYNC_WORD_SIZE + IL2P_HEADER_SIZE + IL2P_HEADER_PARITY + IL2P_MAX_ENCODED_PAYLOAD_SIZE)

    enum il2p_s
    {
        IL2P_SEARCHING = 0,
        IL2P_HEADER,
        IL2P_PAYLOAD,
        IL2P_DECODE
    };

    struct il2p_context_s
    {
        enum il2p_s state;
        unsigned int acc;
        int bc;
        int hc;
        int eplen;
        int pc;
        uint8_t shdr[IL2P_HEADER_SIZE + IL2P_HEADER_PARITY];
        uint8_t uhdr[IL2P_HEADER_SIZE];
        uint8_t spayload[IL2P_MAX_ENCODED_PAYLOAD_SIZE];
    };

    typedef struct
    {
        int payload_byte_count;
        int payload_block_count;
        int small_block_size;
        int large_block_size;
        int large_block_count;
        int small_block_count;
        int parity_symbols_per_block;
    } il2p_payload_properties_t;

#define FEC_MAX_CHECK 64

    struct rs
    {
        uint8_t *alpha_to;
        uint8_t *index_of;
        uint8_t *genpoly;
        uint8_t nroots;
        unsigned int mm;
        unsigned int nn;
        //
        uint8_t fcr;
        uint8_t prim;
        uint8_t iprim;
    };

    static inline unsigned int modnn(struct rs *rs, unsigned int x)
    {
        while (x >= rs->nn)
        {
            x -= rs->nn;
            x = (x >> rs->mm) + (x & rs->nn);
        }

        return x;
    }

    void encode_rs_char(struct rs *, uint8_t *, uint8_t *);
    int decode_rs_char(struct rs *, uint8_t *, int *, int);

    void il2p_init(void);
    struct rs *il2p_find_rs(int);
    void il2p_encode_rs(uint8_t *, int, int, uint8_t *);
    int il2p_decode_rs(uint8_t *, int, int, uint8_t *);
    struct rs *init_rs_char(unsigned int, unsigned int, unsigned int, unsigned int);
    void il2p_rec_bit(int);
    int il2p_send_frame(packet_t);
    void il2p_send_idle(int);
    int il2p_encode_frame(packet_t, uint8_t *);
    packet_t il2p_decode_frame(uint8_t *);
    packet_t il2p_decode_header_payload(uint8_t *, uint8_t *, int *);
    int il2p_type_1_header(packet_t, uint8_t *);
    packet_t il2p_decode_header_type_1(uint8_t *, int);
    int il2p_clarify_header(uint8_t *, uint8_t *);
    void il2p_scramble_block(uint8_t *, uint8_t *, int);
    void il2p_descramble_block(uint8_t *, uint8_t *, int);
    int il2p_payload_compute(il2p_payload_properties_t *, int);
    int il2p_encode_payload(uint8_t *, int, uint8_t *);
    int il2p_decode_payload(uint8_t *, int, uint8_t *, int *);
    int il2p_get_header_attributes(uint8_t *);

#ifdef __cplusplus
}
#endif
