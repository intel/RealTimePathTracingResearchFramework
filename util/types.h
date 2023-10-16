// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <cstddef>

typedef ptrdiff_t index_t;
typedef index_t len_t;

template <class T>
inline len_t len(T&& x) {
    return static_cast<len_t>( x.size() );
}
template <class T>
inline len_t to_len(T size) {
    return static_cast<len_t>(size);
}

void throw_ilen_overflow(int /*to*/, intmax_t /*from*/);

template <class T>
inline int ilen(T&& x) {
    len_t len = static_cast<len_t>( x.size() );
    int ilen = static_cast<int>(len);
    if (ilen != len)
        throw_ilen_overflow(ilen, len);
    return ilen;
}
template <class T, int N>
inline constexpr int array_ilen(T (&x)[N]) {
    return N;
}
template <class T>
inline int to_ilen(T size) {
    len_t len = static_cast<len_t>(size);
    int ilen = static_cast<int>(len);
    if (ilen != len)
        throw_ilen_overflow(ilen, len);
    return ilen;
}

void throw_int_overflow(intmax_t /*to*/, intmax_t /*from*/);

// ensures that the result can safely be casted back to the original integer width
template <class T=int, class I=void>
inline T int_cast(I integer) {
    T to = static_cast<T>(integer);
    bool sign_change = false;
    if ((T(-1) > 0) != (I(-1) > 0)) {
        if (T(-1) > 0 && integer < 0)
            sign_change = true;
        if (I(-1) > 0 && to < 0)
            sign_change = true;
    }
    if (sign_change || static_cast<I>(to) != integer)
        throw_int_overflow(to, integer);
    return to;
}

void throw_uint_overflow(unsigned /*to*/, intmax_t /*from*/);

// ensures that the result is a positive unsigned value that fits within int
template <class I>
inline unsigned uint_bound(I integer) {
    int to = static_cast<int>(integer);
    if (to < 0 || static_cast<I>(to) != integer)
        throw_uint_overflow(static_cast<unsigned>(to), integer);
    return static_cast<unsigned>(to);
}

template <class T>
inline T min(T a, T b) {
    return b < a || a != a ? b : a;
}
template <class T>
inline T max(T a, T b) {
    return a < b || a != a ? b : a;
}

template <class T>
struct mapped_range {
    T* first;
    T* last;
    T* begin() const { return first; }
    T* end() const { return last; }
};
