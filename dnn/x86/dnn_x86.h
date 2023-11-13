/* Copyright (c) 2011-2019 Mozilla
                 2023 Amazon */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef DNN_X86_H
#define DNN_X86_H

#include "cpu_support.h"
#include "opus_types.h"

#if defined(OPUS_X86_MAY_HAVE_SSE2)
void compute_linear_sse2(const LinearLayer *linear, float *out, const float *in);
#endif

#if defined(OPUS_X86_MAY_HAVE_SSE4_1)
void compute_linear_sse4_1(const LinearLayer *linear, float *out, const float *in);
#endif

#if defined(OPUS_X86_MAY_HAVE_AVX2)
void compute_linear_avx2(const LinearLayer *linear, float *out, const float *in);
#endif


#if defined(OPUS_X86_PRESUME_AVX2)

#define OVERRIDE_COMPUTE_LINEAR
#define compute_linear(linear, out, in, arch) ((void)(arch),compute_linear_avx2(linear, out, in))

#elif defined(OPUS_X86_PRESUME_SSE4_1) && !defined(OPUS_X86_MAY_HAVE_AVX2)

#define OVERRIDE_COMPUTE_LINEAR
#define compute_linear(linear, out, in, arch) ((void)(arch),compute_linear_sse4_1(linear, out, in))

#elif defined(OPUS_X86_PRESUME_SSE2) && !defined(OPUS_X86_MAY_HAVE_AVX2) && !defined(OPUS_X86_MAY_HAVE_SSE4_1)

#define OVERRIDE_COMPUTE_LINEAR
#define compute_linear(linear, out, in, arch) ((void)(arch),compute_linear_sse2(linear, out, in))

#elif defined(OPUS_HAVE_RTCD) && (defined(OPUS_X86_MAY_HAVE_AVX2) || defined(OPUS_X86_MAY_HAVE_SSE4_1) || defined(OPUS_X86_MAY_HAVE_SSE2))

extern void (*const DNN_COMPUTE_LINEAR_IMPL[OPUS_ARCHMASK + 1])(
                    const LinearLayer *linear,
                    float *out,
                    const float *in
                    );

#define OVERRIDE_COMPUTE_LINEAR
#define compute_linear(linear, out, in, arch) \
    ((*DNN_COMPUTE_LINEAR_IMPL[(arch) & OPUS_ARCHMASK])(linear, out, in))

#endif



#endif /* DNN_X86_H */
