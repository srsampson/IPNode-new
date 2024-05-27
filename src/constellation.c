/*
 * Copyright 2010-2012,2014,2018 Free Software Foundation, Inc.
 *
 * Fork by Steve Sampson, K5OKC, May 2024
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <math.h>
#include <stdint.h>
#include <complex.h>

#include "constellation.h"

static complex float d_qpsk[4];

void createQPSKConstellation()
{
    // Gray-coded
    d_qpsk[0] = CMPLXF(1.0f, 0.0f);
    d_qpsk[1] = CMPLXF(0.0f, 1.0f);
    d_qpsk[2] = CMPLXF(0.0f, -1.0f);
    d_qpsk[3] = CMPLXF(-1.0f, 0.0f);
}

complex float *getQPSKConstellation()
{
    return d_qpsk;
}

complex float getQPSKQuadrant(uint8_t diBit)
{
    return d_qpsk[diBit];
}

uint8_t qpskToDiBit(complex float sample)
{
    // Real component determines small bit.
    // Imag component determines big bit.
    return 2 * (cimagf(sample) > 0.0f) + (crealf(sample) > 0.0f);
}
