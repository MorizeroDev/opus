/***********************************************************************
Copyright (c) 2006-2011, Skype Limited. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, (subject to the limitations in the disclaimer below)
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific
contributors, may be used to endorse or promote products derived from
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

/* Conversion between prediction filter coefficients and NLSFs  */
/* Requires the order to be an even number                      */
/* A piecewise linear approximation maps LSF <-> cos(LSF)       */
/* Therefore the result is not accurate NLSFs, but the two      */
/* functions are accurate inverses of each other                */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "silk_SigProc_FIX.h"
#include "silk_tables.h"

/* Number of binary divisions, when not in low complexity mode */
#define BIN_DIV_STEPS_A2NLSF_FIX      3 /* must be no higher than 16 - log2( LSF_COS_TAB_SZ_FIX ) */
#define QPoly                        16
#define MAX_ITERATIONS_A2NLSF_FIX    30

/* Flag for using 2x as many cosine sampling points, reduces the risk of missing a root */
#define OVERSAMPLE_COSINE_TABLE       0

/* Helper function for A2NLSF(..)                    */
/* Transforms polynomials from cos(n*f) to cos(f)^n  */
static inline void silk_A2NLSF_trans_poly(
    opus_int32        *p,    /* I/O    Polynomial                                */
    const opus_int    dd     /* I      Polynomial order (= filter order / 2 )    */
)
{
    opus_int k, n;

    for( k = 2; k <= dd; k++ ) {
        for( n = dd; n > k; n-- ) {
            p[ n - 2 ] -= p[ n ];
        }
        p[ k - 2 ] -= SKP_LSHIFT( p[ k ], 1 );
    }
}
/* Helper function for A2NLSF(..)                    */
/* Polynomial evaluation                             */
static inline opus_int32 silk_A2NLSF_eval_poly(    /* return the polynomial evaluation, in QPoly */
    opus_int32        *p,    /* I    Polynomial, QPoly        */
    const opus_int32   x,    /* I    Evaluation point, Q12    */
    const opus_int    dd     /* I    Order                    */
)
{
    opus_int   n;
    opus_int32 x_Q16, y32;

    y32 = p[ dd ];                                    /* QPoly */
    x_Q16 = SKP_LSHIFT( x, 4 );
    for( n = dd - 1; n >= 0; n-- ) {
        y32 = SKP_SMLAWW( p[ n ], y32, x_Q16 );       /* QPoly */
    }
    return y32;
}

static inline void silk_A2NLSF_init(
     const opus_int32    *a_Q16,
     opus_int32          *P,
     opus_int32          *Q,
     const opus_int      dd
)
{
    opus_int k;

    /* Convert filter coefs to even and odd polynomials */
    P[dd] = SKP_LSHIFT( 1, QPoly );
    Q[dd] = SKP_LSHIFT( 1, QPoly );
    for( k = 0; k < dd; k++ ) {
#if( QPoly < 16 )
        P[ k ] = SKP_RSHIFT_ROUND( -a_Q16[ dd - k - 1 ] - a_Q16[ dd + k ], 16 - QPoly ); /* QPoly */
        Q[ k ] = SKP_RSHIFT_ROUND( -a_Q16[ dd - k - 1 ] + a_Q16[ dd + k ], 16 - QPoly ); /* QPoly */
#elif( Qpoly == 16 )
        P[ k ] = -a_Q16[ dd - k - 1 ] - a_Q16[ dd + k ]; // QPoly
        Q[ k ] = -a_Q16[ dd - k - 1 ] + a_Q16[ dd + k ]; // QPoly
#else
        P[ k ] = SKP_LSHIFT( -a_Q16[ dd - k - 1 ] - a_Q16[ dd + k ], QPoly - 16 ); /* QPoly */
        Q[ k ] = SKP_LSHIFT( -a_Q16[ dd - k - 1 ] + a_Q16[ dd + k ], QPoly - 16 ); /* QPoly */
#endif
    }

    /* Divide out zeros as we have that for even filter orders, */
    /* z =  1 is always a root in Q, and                        */
    /* z = -1 is always a root in P                             */
    for( k = dd; k > 0; k-- ) {
        P[ k - 1 ] -= P[ k ];
        Q[ k - 1 ] += Q[ k ];
    }

    /* Transform polynomials from cos(n*f) to cos(f)^n */
    silk_A2NLSF_trans_poly( P, dd );
    silk_A2NLSF_trans_poly( Q, dd );
}

/* Compute Normalized Line Spectral Frequencies (NLSFs) from whitening filter coefficients        */
/* If not all roots are found, the a_Q16 coefficients are bandwidth expanded until convergence.    */
void silk_A2NLSF(
    opus_int16        *NLSF,                 /* O    Normalized Line Spectral Frequencies, Q15 (0 - (2^15-1)), [d]    */
    opus_int32        *a_Q16,                /* I/O  Monic whitening filter coefficients in Q16 [d]                   */
    const opus_int    d                      /* I    Filter order (must be even)                                      */
)
{
    opus_int      i, k, m, dd, root_ix, ffrac;
    opus_int32 xlo, xhi, xmid;
    opus_int32 ylo, yhi, ymid;
    opus_int32 nom, den;
    opus_int32 P[ SILK_MAX_ORDER_LPC / 2 + 1 ];
    opus_int32 Q[ SILK_MAX_ORDER_LPC / 2 + 1 ];
    opus_int32 *PQ[ 2 ];
    opus_int32 *p;

    /* Store pointers to array */
    PQ[ 0 ] = P;
    PQ[ 1 ] = Q;

    dd = SKP_RSHIFT( d, 1 );

    silk_A2NLSF_init( a_Q16, P, Q, dd );

    /* Find roots, alternating between P and Q */
    p = P;    /* Pointer to polynomial */

    xlo = silk_LSFCosTab_FIX_Q12[ 0 ]; // Q12
    ylo = silk_A2NLSF_eval_poly( p, xlo, dd );

    if( ylo < 0 ) {
        /* Set the first NLSF to zero and move on to the next */
        NLSF[ 0 ] = 0;
        p = Q;                      /* Pointer to polynomial */
        ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
        root_ix = 1;                /* Index of current root */
    } else {
        root_ix = 0;                /* Index of current root */
    }
    k = 1;                          /* Loop counter */
    i = 0;                          /* Counter for bandwidth expansions applied */
    while( 1 ) {
        /* Evaluate polynomial */
#if OVERSAMPLE_COSINE_TABLE
        xhi = silk_LSFCosTab_FIX_Q12[   k       >> 1 ] +
          ( ( silk_LSFCosTab_FIX_Q12[ ( k + 1 ) >> 1 ] -
              silk_LSFCosTab_FIX_Q12[   k       >> 1 ] ) >> 1 );    /* Q12 */
#else
        xhi = silk_LSFCosTab_FIX_Q12[ k ]; /* Q12 */
#endif
        yhi = silk_A2NLSF_eval_poly( p, xhi, dd );

        /* Detect zero crossing */
        if( ( ylo <= 0 && yhi >= 0 ) || ( ylo >= 0 && yhi <= 0 ) ) {
            /* Binary division */
#if OVERSAMPLE_COSINE_TABLE
            ffrac = -128;
#else
            ffrac = -256;
#endif
            for( m = 0; m < BIN_DIV_STEPS_A2NLSF_FIX; m++ ) {
                /* Evaluate polynomial */
                xmid = SKP_RSHIFT_ROUND( xlo + xhi, 1 );
                ymid = silk_A2NLSF_eval_poly( p, xmid, dd );

                /* Detect zero crossing */
                if( ( ylo <= 0 && ymid >= 0 ) || ( ylo >= 0 && ymid <= 0 ) ) {
                    /* Reduce frequency */
                    xhi = xmid;
                    yhi = ymid;
                } else {
                    /* Increase frequency */
                    xlo = xmid;
                    ylo = ymid;
#if OVERSAMPLE_COSINE_TABLE
                    ffrac = SKP_ADD_RSHIFT( ffrac,  64, m );
#else
                    ffrac = SKP_ADD_RSHIFT( ffrac, 128, m );
#endif
                }
            }

            /* Interpolate */
            if( SKP_abs( ylo ) < 65536 ) {
                /* Avoid dividing by zero */
                den = ylo - yhi;
                nom = SKP_LSHIFT( ylo, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) + SKP_RSHIFT( den, 1 );
                if( den != 0 ) {
                    ffrac += SKP_DIV32( nom, den );
                }
            } else {
                /* No risk of dividing by zero because abs(ylo - yhi) >= abs(ylo) >= 65536 */
                ffrac += SKP_DIV32( ylo, SKP_RSHIFT( ylo - yhi, 8 - BIN_DIV_STEPS_A2NLSF_FIX ) );
            }
#if OVERSAMPLE_COSINE_TABLE
            NLSF[ root_ix ] = (opus_int16)SKP_min_32( SKP_LSHIFT( (opus_int32)k, 7 ) + ffrac, SKP_int16_MAX );
#else
            NLSF[ root_ix ] = (opus_int16)SKP_min_32( SKP_LSHIFT( (opus_int32)k, 8 ) + ffrac, SKP_int16_MAX );
#endif

            SKP_assert( NLSF[ root_ix ] >=     0 );
            SKP_assert( NLSF[ root_ix ] <= 32767 );

            root_ix++;        /* Next root */
            if( root_ix >= d ) {
                /* Found all roots */
                break;
            }
            /* Alternate pointer to polynomial */
            p = PQ[ root_ix & 1 ];

            /* Evaluate polynomial */
#if OVERSAMPLE_COSINE_TABLE
            xlo = silk_LSFCosTab_FIX_Q12[ ( k - 1 ) >> 1 ] +
              ( ( silk_LSFCosTab_FIX_Q12[   k       >> 1 ] -
                  silk_LSFCosTab_FIX_Q12[ ( k - 1 ) >> 1 ] ) >> 1 ); // Q12
#else
            xlo = silk_LSFCosTab_FIX_Q12[ k - 1 ]; // Q12
#endif
            ylo = SKP_LSHIFT( 1 - ( root_ix & 2 ), 12 );
        } else {
            /* Increment loop counter */
            k++;
            xlo    = xhi;
            ylo    = yhi;

#if OVERSAMPLE_COSINE_TABLE
            if( k > 2 * LSF_COS_TAB_SZ_FIX ) {
#else
            if( k > LSF_COS_TAB_SZ_FIX ) {
#endif
                i++;
                if( i > MAX_ITERATIONS_A2NLSF_FIX ) {
                    /* Set NLSFs to white spectrum and exit */
                    NLSF[ 0 ] = (opus_int16)SKP_DIV32_16( 1 << 15, d + 1 );
                    for( k = 1; k < d; k++ ) {
                        NLSF[ k ] = (opus_int16)SKP_SMULBB( k + 1, NLSF[ 0 ] );
                    }
                    return;
                }

                /* Error: Apply progressively more bandwidth expansion and run again */
                silk_bwexpander_32( a_Q16, d, 65536 - SKP_SMULBB( 10 + i, i ) ); // 10_Q16 = 0.00015

                silk_A2NLSF_init( a_Q16, P, Q, dd );
                p = P;                            /* Pointer to polynomial */
                xlo = silk_LSFCosTab_FIX_Q12[ 0 ]; // Q12
                ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
                if( ylo < 0 ) {
                    /* Set the first NLSF to zero and move on to the next */
                    NLSF[ 0 ] = 0;
                    p = Q;                        /* Pointer to polynomial */
                    ylo = silk_A2NLSF_eval_poly( p, xlo, dd );
                    root_ix = 1;                  /* Index of current root */
                } else {
                    root_ix = 0;                  /* Index of current root */
                }
                k = 1;                            /* Reset loop counter */
            }
        }
    }
}
