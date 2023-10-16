// Copyright 2023 Intel Corporation.
// SPDX-License-Identifier: MIT

#pragma once

/*
 * T is the sample type, AT is the type used for aggregate values.
 */
template <class T, class AT=float>
struct OnlineStats
{
    long long num_samples {0};
    T current_sample {0};
    T sample_min { std::numeric_limits<T>::max() };
    T sample_max { -std::numeric_limits<T>::max() };
    AT sample_mean {0};
    AT sample_variance {0};
    AT sample_stddev {0};
    AT exponential_moving_average {0};

    inline void update(const T &new_sample);

    private:
        AT S {0};
};

template <class T, class AT>
void OnlineStats<T, AT>::update(const T &new_sample)
{
    ++num_samples;
    current_sample = new_sample;
    sample_min = std::min<T>(sample_min, new_sample);
    sample_max = std::max<T>(sample_max, new_sample);

    const AT atsample = static_cast<AT>(new_sample);

    // This is Welford's algorithm, see e.g. Knuth, TAOCP Vol. 2, 4.2.2.
    if (num_samples == 1) {
        sample_mean = atsample;
        exponential_moving_average = atsample;
        S = T{0};
        sample_variance = T{0};
        sample_stddev = T{0};
    } else {
        const T ema_factor = T{2}/T{10};
        exponential_moving_average = 
            exponential_moving_average * (T{1}-ema_factor)
            + atsample * ema_factor;

        const T d1 = atsample - sample_mean;
        sample_mean += d1 / static_cast<AT>(num_samples);
        const T d2 = atsample - sample_mean;
        S += d2 * d1;
        sample_variance = S / static_cast<AT>(num_samples-1);
        sample_stddev = std::sqrt(sample_variance);
    }
}
