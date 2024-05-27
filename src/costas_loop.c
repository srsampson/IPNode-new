/*
 * Copyright 2006,2011,2012,2014 Free Software Foundation, Inc.
 * Author: Tom Rondeau, 2011
 *
 * Fork by Steve Sampson, K5OKC, May 2024
 * 
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <stdbool.h>
#include <complex.h>
#include <math.h>

#include "costas_loop.h"
#include "ipnode.h"

static void update_gains(void);

static float d_phase;
static float d_freq;

static float d_max_freq;
static float d_min_freq;

static float d_damping;
static float d_loop_bw;

static float d_alpha;
static float d_beta;

/*
 * A Costas loop carrier recovery algorithm.
 *
 * The Costas loop locks to the center frequency of a signal and
 * downconverts signal to baseband.
 */
void create_control_loop(float loop_bw, float min_freq, float max_freq) {
    set_phase(0.0f);
    set_frequency(0.0f);

    set_max_freq(max_freq);
    set_min_freq(min_freq);

    set_damping_factor(sqrtf(2.0f) / 2.0f);

    // Calls update_gains() which sets alpha and beta
    set_loop_bandwidth(loop_bw);
}

/*
 * QPSK error signal following a limiter function
 * We limit the output to +/- 1
 * 
 * This should be low pass filtered
 */
float phase_detector(complex float sample) {
    float re = crealf(sample);
    float im = cimagf(sample);

    /*
     * This algorithm needs a zero case
     * Even though it's not likely, as a limiter
     */
    if ((fabsf(re) == 0.0f) || (fabsf(im) == 0.0f)) {
        return 0.0f;
    }

    float real_limit = ((re > 0.0f) ? 1.0f : -1.0f);
    float imag_limit = ((im > 0.0f) ? 1.0f : -1.0f);

    return ((real_limit * im) - (imag_limit * re));
}

static void update_gains() {
    float denom = ((1.0f + (2.0f * d_damping * d_loop_bw)) + (d_loop_bw * d_loop_bw));

    d_alpha = (4.0f * d_damping * d_loop_bw) / denom;
    d_beta = (4.0f * d_loop_bw * d_loop_bw) / denom;
}

void advance_loop(float error) {
    d_freq += (d_beta * error);
    d_phase += (d_freq + d_alpha * error);
}

void phase_wrap() {
    while (d_phase > TAU)
        d_phase -= TAU;

    while (d_phase < -TAU)
        d_phase += TAU;
}

void frequency_limit() {
    if (d_freq > d_max_freq)
        d_freq = d_max_freq;
    else if (d_freq < d_min_freq)
        d_freq = d_min_freq;
}


// Setters

void set_loop_bandwidth(float bw)
{
    if (bw < 0.0f) {
        d_loop_bw = 0.0f;
    }

    d_loop_bw = bw;
    update_gains();
}

void set_damping_factor(float df)
{
    if (df <= 0.0f) {
        d_damping = 0.0f;
    }

    d_damping = df;
    update_gains();
}

void set_alpha(float alpha)
{
    if (alpha < 0.0f || alpha > 1.0f) {
        d_alpha = 0.0f;
    }

    d_alpha = alpha;
}

void set_beta(float beta)
{
    if (beta < 0.0f || beta > 1.0f) {
        d_beta = 0.0f;
    }

    d_beta = beta;
}

void set_frequency(float freq)
{
    if (freq > d_max_freq)
        d_freq = d_max_freq;
    else if (freq < d_min_freq)
        d_freq = d_min_freq;
    else
        d_freq = freq;
}

void set_phase(float phase)
{
    d_phase = phase;

    phase_wrap();
}

void set_max_freq(float freq) { d_max_freq = freq; }

void set_min_freq(float freq) { d_min_freq = freq; }

// Getters

float get_loop_bandwidth() { return d_loop_bw; }

float get_damping_factor() { return d_damping; }

float get_alpha() { return d_alpha; }

float get_beta() { return d_beta; }

float get_frequency() { return d_freq; }

float get_phase() { return d_phase; }

float get_max_freq() { return d_max_freq; }

float get_min_freq() { return d_min_freq; }

