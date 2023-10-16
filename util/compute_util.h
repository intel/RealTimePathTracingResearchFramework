// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <glm/glm.hpp>

float srgb_to_linear(const float x);

float linear_to_srgb(const float x);

float luminance(const glm::vec3 &c);

void ortho_basis(glm::vec3 &v_x, glm::vec3 &v_y, const glm::vec3 &n);

// Copyright (c) 2023 Leonhard Gruenschloss
// MIT license, https://github.com/lgruen/halton
// Special case: radical inverse in base 2, with direct bit reversal.
inline float halton2(unsigned index)
{
    index = (index << 16) | (index >> 16);
    index = ((index & 0x00ff00ff) << 8) | ((index & 0xff00ff00) >> 8);
    index = ((index & 0x0f0f0f0f) << 4) | ((index & 0xf0f0f0f0) >> 4);
    index = ((index & 0x33333333) << 2) | ((index & 0xcccccccc) >> 2);
    index = ((index & 0x55555555) << 1) | ((index & 0xaaaaaaaa) >> 1);
    union Result
    {
        unsigned u;
        float f;
    } result; // Write reversed bits directly into floating-point mantissa.
    result.u = 0x3f800000u | (index >> 9);
    return result.f - 1.f;
}

// float->half variants.
// by Fabian "ryg" Giesen.
//
// I hereby place this code in the public domain, as per the terms of the
// CC0 license:
//
//   https://creativecommons.org/publicdomain/zero/1.0/
//
// float_to_half_full: This is basically the ISPC stdlib code, except
// I preserve the sign of NaNs (any good reason not to?)
//
// float_to_half_fast: Almost the same, with some unnecessary cases cut.
//
// float_to_half_fast2: This is where it gets a bit weird. See lengthy
// commentary inside the function code. I'm a bit on the fence about two
// things:
// 1. This *will* behave differently based on whether flush-to-zero is
//    enabled or not. Is this acceptable for ISPC?
// 2. I'm a bit on the fence about NaNs. For half->float, I opted to extend
//    the mantissa (preserving both qNaN and sNaN contents) instead of always
//    returning a qNaN like the original ISPC stdlib code did. For float->half
//    the "natural" thing would be just taking the top mantissa bits, except
//    that doesn't work; if they're all zero, we might turn a sNaN into an
//    Infinity (seriously bad!). I could test for this case and do a sticky
//    bit-like mechanism, but that's pretty ugly. Instead I go with ISPC
//    std lib behavior in this case and just return a qNaN - not quite symmetric
//    but at least it's always safe. Any opinions?
//
// I'll just go ahead and give "fast2" the same treatment as my half->float code,
// but if there's concerns with the way it works I might revise it later, so watch
// this spot.
//
// float_to_half_fast3: Bitfields removed. Ready for SSE2-ification :)
//
// float_to_half_SSE2: Exactly what it says on the tin. Beware, this works slightly
// differently from float_to_half_fast3 - the clamp and bias steps in the "normal" path
// are interchanged, since I get "minps" on every SSE2 target, but "pminsd" only for
// SSE4.1 targets. This code does what it should and is remarkably short, but the way
// it ended up working is "nonobvious" to phrase it politely.
//
// approx_float_to_half: Simpler (but less accurate) version that matches the Fox
// toolkit float->half conversions: http://blog.fox-toolkit.org/?p=40 - note that this
// also (incorrectly) translates some sNaNs into infinity, so be careful!
//
// approx_float_to_half_SSE2: SSE2 version of above.
//
// ----
//
// UPDATE 2016-01-25: Now also with a variant that implements proper round-to-nearest-even.
// It's a bit more expensive and has seen less tweaking than the other variants. On the
// plus side, it doesn't produce subnormal FP32 values as part of generating subnormal
// FP16 values, so the performance is a lot more consistent.
//
// float_to_half_rtne_full: Unoptimized round-to-nearest-break-ties-to-even reference
// implementation.
// 
// float_to_half_fast3_rtne: Variant of float_to_half_fast3 that performs round-to-
// nearest-even.
//
// float_to_half_rtne_SSE2: SSE2 implementation of float_to_half_fast3_rtne.
//
// All three functions have been exhaustively tested to produce the same results on
// all 32-bit floating-point numbers with SSE arithmetic in round-to-nearest-even mode.
// No guarantees for what happens with other rounding modes! (See testbed code.)
//
// ----
//
// Oh, and enumerating+testing all 32-bit floats takes some time, especially since
// we will snap a significant fraction of the overall FP32 range to denormals, not
// exactly a fast operation. There's a reason this one prints regular progress
// reports. You've been warned.

#include <stdlib.h>

union FP32
{
    unsigned u;
    float f;
    struct
    {
        unsigned Mantissa : 23;
        unsigned Exponent : 8;
        unsigned Sign : 1;
    };
};

union FP16
{
    unsigned short u;
    struct
    {
        unsigned Mantissa : 10;
        unsigned Exponent : 5;
        unsigned Sign : 1;
    };
};

// Original ISPC reference version; this always rounds ties up.
inline FP16 float_to_half_rup(FP32 f)
{
    FP16 o = { 0 };

    // Based on ISPC reference code (with minor modifications)
    if (f.Exponent == 0) // Signed zero/denormal (which will underflow)
        o.Exponent = 0;
    else if (f.Exponent == 255) // Inf or NaN (all exponent bits set)
    {
        o.Exponent = 31;
        o.Mantissa = f.Mantissa ? 0x200 : 0; // NaN->qNaN and Inf->Inf
    }
    else // Normalized number
    {
        // Exponent unbias the single, then bias the halfp
        int newexp = f.Exponent - 127 + 15;
        if (newexp >= 31) // Overflow, return signed infinity
            o.Exponent = 31;
        else if (newexp <= 0) // Underflow
        {
            if ((14 - newexp) <= 24) // Mantissa might be non-zero
            {
                unsigned mant = f.Mantissa | 0x800000; // Hidden 1 bit
                o.Mantissa = mant >> (14 - newexp);
                if ((mant >> (13 - newexp)) & 1) // Check for rounding
                    o.u++; // Round, might overflow into exp bit, but this is OK
            }
        }
        else
        {
            o.Exponent = newexp;
            o.Mantissa = f.Mantissa >> 13;
            if (f.Mantissa & 0x1000) // Check for rounding
                o.u++; // Round, might overflow to inf, this is OK
        }
    }

    o.Sign = f.Sign;
    return o;
}

// Same as above, but with full round-to-nearest-even.
inline FP16 float_to_half(FP32 f)
{
    FP16 o = { 0 };

    // Based on ISPC reference code (with minor modifications)
    if (f.Exponent == 0) // Signed zero/denormal (which will underflow)
        o.Exponent = 0;
    else if (f.Exponent == 255) // Inf or NaN (all exponent bits set)
    {
        o.Exponent = 31;
        o.Mantissa = f.Mantissa ? 0x200 : 0; // NaN->qNaN and Inf->Inf
    }
    else // Normalized number
    {
        // Exponent unbias the single, then bias the halfp
        int newexp = f.Exponent - 127 + 15;
        if (newexp >= 31) // Overflow, return signed infinity
            o.Exponent = 31;
        else if (newexp <= 0) // Underflow
        {
            if ((14 - newexp) <= 24) // Mantissa might be non-zero
            {
                unsigned mant = f.Mantissa | 0x800000; // Hidden 1 bit
                unsigned shift = 14 - newexp;
                o.Mantissa = mant >> shift;

                unsigned lowmant = mant & ((1 << shift) - 1);
                unsigned halfway = 1 << (shift - 1);

                if (lowmant >= halfway) // Check for rounding
                {
                    if (lowmant > halfway || (o.Mantissa & 1)) // if above halfway point or unrounded result is odd
                        o.u++; // Round, might overflow into exp bit, but this is OK
                }
            }
        }
        else
        {
            o.Exponent = newexp;
            o.Mantissa = f.Mantissa >> 13;
            if (f.Mantissa & 0x1000) // Check for rounding
            {
                if (((f.Mantissa & 0x1fff) > 0x1000) || (o.Mantissa & 1)) // if above halfway point or unrounded result is odd
                    o.u++; // Round, might overflow to inf, this is OK
            }
        }
    }

    o.Sign = f.Sign;
    return o;
}
