/*
 * il2p_init.c
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#include "ipnode.h"
#include "il2p.h"

#define NTAB 5
#define BLOCK_SIZE 255

#define min(a, b) ((a) < (b) ? (a) : (b))

static struct
{
    unsigned int genpoly; // Field generator polynomial coefficients.
    unsigned int fcs;     // First root of RS code generator polynomial, index form, IL2P uses 0.
    unsigned int prim;    // Primitive element to generate polynomial roots.
    unsigned int nroots;  // RS code generator polynomial degree (number of roots).
                          // Same as number of check bytes added.
    struct rs *rs; // Pointer to RS codec control block.  Filled in at init time.
} Tab[NTAB] = {
    {0x11d, 0, 1, 2, (struct rs *) NULL},  // 2 parity
    {0x11d, 0, 1, 4, (struct rs *) NULL},  // 4 parity
    {0x11d, 0, 1, 6, (struct rs *) NULL},  // 6 parity
    {0x11d, 0, 1, 8, (struct rs *) NULL},  // 8 parity
    {0x11d, 0, 1, 16, (struct rs *) NULL}, // 16 parity
};

void il2p_init()
{
    for (int i = 0; i < NTAB; i++)
    {
        Tab[i].rs = init_rs_char(Tab[i].genpoly, Tab[i].fcs, Tab[i].prim, Tab[i].nroots);

        if (Tab[i].rs == NULL)
        {
            fprintf(stderr, "il2p_init: Initialize FEC table structure failed\n");
            exit(EXIT_FAILURE);
        }
    }
}

// Find RS codec control block for specified number of parity symbols.

struct rs *il2p_find_rs(int nparity)
{
    for (int n = 0; n < NTAB; n++)
    {
        if (Tab[n].nroots == nparity)
        {
            return Tab[n].rs;
        }
    }

    // Default to 2 Parity

    return Tab[0].rs;
}

void encode_rs_char(struct rs *rs, uint8_t *data, uint8_t *bb)
{
    memset(bb, 0, rs->nroots * sizeof(uint8_t)); // clear out the FEC data area

    for (int i = 0; i < (rs->nn - rs->nroots); i++)
    {
        uint8_t feedback = rs->index_of[data[i] ^ bb[0]];

        if (feedback != rs->nn) // feedback term is non-zero
        {
            for (int j = 1; j < rs->nroots; j++)
            {
                bb[j] ^= rs->alpha_to[modnn(rs, (feedback + rs->genpoly[rs->nroots - j]))];
            }
        }

        // Shift
        memmove(&bb[0], &bb[1], sizeof(uint8_t) * (rs->nroots - 1));

        if (feedback != rs->nn)
        {
            bb[rs->nroots - 1] = rs->alpha_to[modnn(rs, (feedback + rs->genpoly[0]))];
        }
        else
        {
            bb[rs->nroots - 1] = 0U;
        }
    }
}

void il2p_encode_rs(uint8_t *tx_data, int data_size, int num_parity, uint8_t *parity_out)
{
    uint8_t rs_block[BLOCK_SIZE] = { 0 };

    memcpy(rs_block + sizeof(rs_block) - data_size - num_parity, tx_data, data_size);

    encode_rs_char(il2p_find_rs(num_parity), rs_block, parity_out);
}

int decode_rs_char(struct rs *restrict rs, uint8_t *restrict data, int *eras_pos, int no_eras)
{
    uint8_t lambda[FEC_MAX_CHECK + 1];
    uint8_t s[FEC_MAX_CHECK]; // Err+Eras Locator poly and syndrome poly
    uint8_t t[FEC_MAX_CHECK + 1];
    uint8_t root[FEC_MAX_CHECK];
    uint8_t reg[FEC_MAX_CHECK + 1];
    uint8_t loc[FEC_MAX_CHECK];
    uint8_t omega[FEC_MAX_CHECK + 1];
    uint8_t b[FEC_MAX_CHECK + 1];
    int count;

    /* form the syndromes; i.e., evaluate data(x) at roots of g(x) */
    for (int i = 0; i < rs->nroots; i++)
    {
        s[i] = data[0];
    }

    for (int j = 1; j < rs->nn; j++)
    {
        for (int i = 0; i < rs->nroots; i++)
        {
            if (s[i] == 0U)
            {
                s[i] = data[j];
            }
            else
            {
                s[i] = data[j] ^ rs->alpha_to[modnn(rs, (rs->index_of[s[i]] + (rs->fcr + i) * rs->prim))];
            }
        }
    }

    /* Convert syndromes to index form, checking for nonzero condition */
    unsigned int syn_error = 0;

    for (unsigned int i = 0U; i < rs->nroots; i++)
    {
        syn_error |= s[i];
        s[i] = rs->index_of[s[i]];
    }

    if (syn_error == 0U)
    {
        /* if syndrome is zero, data[] is a codeword and there are no
         * errors to correct. So return data[] unmodified
         */
        count = 0;
        goto finish;
    }

    uint8_t tmp = 0U;

    memset(&lambda[1], 0, rs->nroots * sizeof(lambda[0]));
    lambda[0] = 1U;

    if (no_eras > 0)
    {
        /* Init lambda to be the erasure locator polynomial */
        lambda[1] = rs->alpha_to[modnn(rs, (rs->prim * (rs->nn - 1U - eras_pos[0])))];

        for (int i = 1; i < no_eras; i++)
        {
            uint8_t u = modnn(rs, (rs->prim * (rs->nn - 1U - eras_pos[i])));

            for (int j = i + 1; j > 0; j--)
            {
                tmp = rs->index_of[lambda[j - 1]];

                if (tmp != rs->nn)
                    lambda[j] ^= rs->alpha_to[modnn(rs, u + tmp)];
            }
        }
    }

    for (int i = 0; i < (rs->nroots + 1); i++)
    {
        b[i] = rs->index_of[lambda[i]];
    }

    /*
     * Begin Berlekamp-Massey algorithm to determine error+erasure
     * locator polynomial
     */
    int r = no_eras; // r is the step number
    int el = no_eras;

    while (++r <= rs->nroots)
    {
        /* Compute discrepancy at the r-th step in poly-form */
        uint8_t discr_r = 0U;

        for (int i = 0; i < r; i++)
        {
            if ((lambda[i] != 0U) && (s[r - i - 1] != rs->nn))
            {
                discr_r ^= rs->alpha_to[modnn(rs, (rs->index_of[lambda[i]] + s[r - i - 1]))];
            }
        }

        discr_r = rs->index_of[discr_r]; /* Index form */

        if (discr_r == rs->nn)
        {
            /* 2 lines below: B(x) <-- x*B(x) */
            memmove(&b[1], b, rs->nroots * sizeof(b[0]));
            b[0] = rs->nn; // uint8_t = uint32_t ??
        }
        else
        {
            /* 7 lines below: T(x) <-- lambda(x) - discr_r*x*b(x) */
            t[0] = lambda[0];

            for (int i = 0; i < rs->nroots; i++)
            {
                if (b[i] != rs->nn)
                    t[i + 1] = lambda[i + 1] ^ rs->alpha_to[modnn(rs, discr_r + b[i])];
                else
                    t[i + 1] = lambda[i + 1];
            }

            if (2 * el <= r + no_eras - 1)
            {
                el = r + no_eras - el;
                /*
                 * 2 lines below: B(x) <-- inv(discr_r) *
                 * lambda(x)
                 */
                for (int i = 0; i <= rs->nroots; i++)
                {
                    b[i] = (lambda[i] == 0U) ? rs->nn : modnn(rs, (rs->index_of[lambda[i]]) - discr_r + rs->nn);
                }
            }
            else
            {
                /* 2 lines below: B(x) <-- x*B(x) */
                memmove(&b[1], b, rs->nroots * sizeof(b[0]));
                b[0] = rs->nn;
            }

            memcpy(lambda, t, (rs->nroots + 1) * sizeof(t[0]));
        }
    }

    /* Convert lambda to index form and compute deg(lambda(x)) */
    int deg_lambda = 0;

    for (int i = 0; i < (rs->nroots + 1); i++)
    {
        lambda[i] = rs->index_of[lambda[i]];

        if (lambda[i] != rs->nn)
        {
            deg_lambda = i;
        }
    }

    /* Find roots of the error+erasure locator polynomial by Chien search */
    memcpy(&reg[1], &lambda[1], rs->nroots * sizeof(reg[0]));
    count = 0; /* Number of roots of lambda(x) */

    for (uint8_t i = 1, k = (rs->iprim - 1); i <= rs->nn; i++, k = modnn(rs, (k + rs->iprim)))
    {
        uint8_t q = 1U; /* lambda[0] is always 0 */

        for (int j = deg_lambda; j > 0; j--)
        {
            if (reg[j] != rs->nn)
            {
                reg[j] = modnn(rs, reg[j] + j);
                q ^= rs->alpha_to[reg[j]];
            }
        }

        if (q != 0U)
            continue; /* Not a root */

        /* store root (index-form) and error location number */

        root[count] = i;
        loc[count] = k;

        /* If we've already found max possible roots,
         * abort the search to save time
         */
        if (++count == deg_lambda)
            break;
    }

    if (deg_lambda != count)
    {
        /*
         * deg(lambda) unequal to number of roots => uncorrectable
         * error detected
         */
        count = -1;
        goto finish;
    }

    /*
     * Compute err+eras evaluator poly omega(x) = s(x)*lambda(x) (modulo
     * x**rs->nroots). in index form. Also find deg(omega).
     */
    unsigned int deg_omega = 0;

    for (unsigned int i = 0U; i < rs->nroots; i++)
    {
        tmp = 0U;

        for (int j = (deg_lambda < i) ? deg_lambda : i; j >= 0; j--)
        {
            if ((s[i - j] != rs->nn) && (lambda[j] != rs->nn))
                tmp ^= rs->alpha_to[modnn(rs, (s[i - j] + lambda[j]))];
        }

        if (tmp != 0U)
            deg_omega = i;

        omega[i] = rs->index_of[tmp];
    }

    omega[rs->nroots] = rs->nn;

    /*
     * Compute error values in poly-form. num1 = omega(inv(X(l))), num2 =
     * inv(X(l))**(rs->fcr-1) and den = lambda_pr(inv(X(l))) all in poly-form
     */
    for (int j = count - 1; j >= 0; j--)
    {
        uint8_t num1 = 0U;

        for (unsigned int i = deg_omega; i >= 0; i--)
        {
            if (omega[i] != rs->nn)
            {
                num1 ^= rs->alpha_to[modnn(rs, omega[i] + i * root[j])];
            }
        }

        uint8_t num2 = rs->alpha_to[modnn(rs, (root[j] * (rs->fcr - 1)) + rs->nn)];
        uint8_t den = 0U;

        /* lambda[i+1] for i even is the formal derivative lambda_pr of lambda[i] */
        for (int i = min(deg_lambda, rs->nroots - 1) & ~1; i >= 0; i -= 2)
        {
            if (lambda[i + 1] != rs->nn)
                den ^= rs->alpha_to[modnn(rs, (lambda[i + 1] + i * root[j]))];
        }

        if (den == 0U)
        {
            count = -1;
            break;
        }

        /* Apply error to data */
        if (num1 != 0U)
        {
            data[loc[j]] ^= rs->alpha_to[modnn(rs, (rs->index_of[num1] + rs->index_of[num2] + rs->nn - rs->index_of[den]))];
        }
    }

finish:
    if (eras_pos != NULL)
    {
        for (int i = 0; i < count; i++)
            eras_pos[i] = loc[i];
    }

    return count;
}

int il2p_decode_rs(uint8_t *rec_block, int data_size, int num_parity, uint8_t *out)
{
    //  Use zero padding in front if data size is too small.

    int n = data_size + num_parity; // total size in.

    uint8_t rs_block[BLOCK_SIZE];

    memset(rs_block, 0, sizeof(rs_block) - n);
    memcpy(rs_block + sizeof(rs_block) - n, rec_block, n);

    int derrlocs[FEC_MAX_CHECK]; // Half would probably be OK.

    int derrors = decode_rs_char(il2p_find_rs(num_parity), rs_block, derrlocs, 0);
    memcpy(out, rs_block + sizeof(rs_block) - n, data_size);

    // It is possible to have a situation where too many errors are
    // present but the algorithm could get a good code block by "fixing"
    // one of the padding bytes that should be 0.

    for (int i = 0; i < derrors; i++)
    {
        if (derrlocs[i] < sizeof(rs_block) - n)
        {
            derrors = -1;
            break;
        }
    }

    return derrors;
}

struct rs *init_rs_char(unsigned int gfpoly, unsigned int fcr, unsigned int prim, unsigned int nroots)
{
    if (fcr >= 256)
        return NULL;

    if (prim == 0 || prim >= 256)
        return NULL;

    if (nroots >= 256)
        return NULL;

    struct rs *rs = (struct rs *)calloc(1, sizeof(struct rs));

    if (rs == NULL)
    {
        fprintf(stderr, "Out of memory in FEC init\n");
        exit(EXIT_FAILURE);
    }

    rs->mm = 8;
    rs->nn = 256 - 1;

    rs->alpha_to = (uint8_t *)calloc((rs->nn + 1), sizeof(uint8_t));

    if (rs->alpha_to == NULL)
    {
        fprintf(stderr, "Out of memory in FEC alpha_to\n");

        free(rs);

        return NULL;
    }

    rs->index_of = (uint8_t *)calloc((rs->nn + 1), sizeof(uint8_t));

    if (rs->index_of == NULL)
    {
        fprintf(stderr, "Out of memory in FEC index_of\n");

        free(rs->alpha_to);
        free(rs);

        return NULL;
    }

    /* Generate Galois field lookup tables */
    rs->index_of[0] = rs->nn; /* log(zero) = -inf */
    rs->alpha_to[rs->nn] = 0; /* alpha**-inf = 0 */

    unsigned int sr = 1;

    for (unsigned int i = 0; i < rs->nn; i++)
    {
        rs->index_of[sr] = i;
        rs->alpha_to[i] = sr;
        sr <<= 1;

        if (sr & 256)
            sr ^= gfpoly;

        sr &= rs->nn;
    }

    if (sr != 1)
    {
        /* field generator polynomial is not primitive! */
        fprintf(stderr, "Field generator polynomial is not primitive in FEC\n");

        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);

        return NULL;
    }

    /* Form RS code generator polynomial from its roots */
    rs->genpoly = (uint8_t *)calloc((nroots + 1), sizeof(uint8_t));

    if (rs->genpoly == NULL)
    {
        fprintf(stderr, "Out of memory in FEC genpoly\n");

        free(rs->alpha_to);
        free(rs->index_of);
        free(rs);

        return NULL;
    }

    rs->fcr = fcr;
    rs->prim = prim;
    rs->nroots = nroots;

    /* Find prim-th root of 1, used in decoding */

    unsigned int iprim;

    for (iprim = 1; (iprim % prim) != 0; iprim += rs->nn)
        ;

    rs->iprim = (iprim / prim);

    rs->genpoly[0] = 1;

    unsigned int root = (fcr * prim);

    for (int i = 0; i < nroots; i++)
    {
        rs->genpoly[i + 1] = 1;

        /* Multiply rs->genpoly[] by  @**(root + x) */
        for (int j = i; j > 0; j--)
        {
            if (rs->genpoly[j] != 0)
                rs->genpoly[j] = rs->genpoly[j - 1] ^ rs->alpha_to[modnn(rs, (rs->index_of[rs->genpoly[j]]) + root)];
            else
                rs->genpoly[j] = rs->genpoly[j - 1];
        }

        /* rs->genpoly[0] can never be zero */
        rs->genpoly[0] = rs->alpha_to[modnn(rs, (rs->index_of[rs->genpoly[0]]) + root)];

        root += prim;
    }

    /* convert rs->genpoly[] to index form for quicker encoding */

    for (int i = 0U; i <= nroots; i++)
    {
        rs->genpoly[i] = rs->index_of[rs->genpoly[i]];
    }

    return rs;
}
