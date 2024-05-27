/*
 * transmit_queue.h
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

#include "ax25_pad.h"
#include "audio.h"

#define TQ_PRIO_0_HI 0
#define TQ_PRIO_1_LO 1
#define TQ_NUM_PRIO 2

#define il2p_mutex_lock(x)                                                                                    \
  {                                                                                                           \
    int err = pthread_mutex_lock(x);                                                                          \
    if (err != 0)                                                                                             \
    {                                                                                                         \
      fprintf(stderr, "il2p_mutex_lock: INTERNAL ERROR %s %d returned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                \
    }                                                                                                         \
  }

#define il2p_mutex_try_lock(x)                                                                                   \
  ({                                                                                                             \
    int err = pthread_mutex_trylock(x);                                                                          \
    if (err != 0 && err != EBUSY)                                                                                \
    {                                                                                                            \
      fprintf(stderr, "il2p_mutex_try_lock: INTERNAL ERROR %s %d eturned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                   \
    };                                                                                                           \
    !err;                                                                                                        \
  })

#define il2p_mutex_unlock(x)                                                                                    \
  {                                                                                                             \
    int err = pthread_mutex_unlock(x);                                                                          \
    if (err != 0)                                                                                               \
    {                                                                                                           \
      fprintf(stderr, "pthread_mutex_unlock: INTERNAL ERROR %s %d returned %d", __FILE__, __LINE__, err); \
      exit(1);                                                                                                  \
    }                                                                                                           \
  }

  void transmit_queue_init(void);
  void transmit_queue_append(int, packet_t);
  void lm_data_request(int, packet_t);
  void lm_seize_request(void);
  void transmit_queue_wait_while_empty(void);
  packet_t transmit_queue_remove(int);
  packet_t transmit_queue_peek(int);

#ifdef __cplusplus
}
#endif
