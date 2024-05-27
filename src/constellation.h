/*
 * Copyright 2010-2012 Free Software Foundation, Inc.
 *
 * Fork by Steve Sampson, K5OKC, May 2024
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include <complex.h>
#include <stdint.h>

void createQPSKConstellation(void);
complex float *getQPSKConstellation(void);
complex float getQPSKQuadrant(uint8_t);
uint8_t qpskToDiBit(complex float);

#ifdef __cplusplus
}
#endif

