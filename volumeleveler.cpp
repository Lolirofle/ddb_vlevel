// Copyright 2003 Tom Felker
//
// This file is part of VLevel.
//
// VLevel is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// VLevel is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with VLevel; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

// volumeleveler.cpp - defines the VolumeLeveler class

#include <sys/types.h>
#include <math.h>

#include "vlevel.h"
#include "volumeleveler.h"

using namespace std;

VolumeLeveler::VolumeLeveler(size_t l, size_t c, value_t s, value_t m) : m_bufs(NULL)
{
    SetSamplesAndChannels(l, c);
    SetStrength(s);
    SetMaxMultiplier(m);
}

VolumeLeveler::~VolumeLeveler()
{
    if (m_bufs) {
        for (size_t ch = 0; ch < m_channels; ++ch) delete[] m_bufs[ch];
        delete[] m_bufs;
    }
}

void VolumeLeveler::SetStrength(value_t s)
{
    m_strength = s;
}

void VolumeLeveler::SetMaxMultiplier(value_t m)
{
    if (m <= 0) m = HUGE_VAL;
    m_max_multiplier = m;
}

void VolumeLeveler::SetSamplesAndChannels(size_t s, size_t c)
{
    if (m_bufs) {
        for (size_t ch = 0; ch < m_channels; ++ch) delete[] m_bufs[ch];
        delete[] m_bufs;
        m_bufs = NULL;
    }

    if (c > 0) {
        m_bufs = new value_t*[c];
        for (size_t ch = 0; ch < c; ++ch) m_bufs[ch] = new value_t[s];
    }

    m_samples = s;
    m_channels = c;
    Flush();
}

void VolumeLeveler::Flush()
{
    if (m_bufs) {
        for (size_t ch = 0; ch < m_channels; ++ch) {
            for (size_t i = 0; i < m_samples; ++i) {
                m_bufs[ch][i] = 0;
            }
        }
    }

    m_silence = m_samples;
    m_pos = m_max_slope_pos = 0;
    m_max_slope = m_max_slope_val = m_avg_amp = 0;
}

value_t VolumeLeveler::GetMultiplier()
{
    value_t multiplier = (value_t)pow(m_avg_amp, -m_strength);
    if (multiplier > m_max_multiplier) multiplier = m_max_multiplier;
    return multiplier;
}

size_t VolumeLeveler::Exchange(value_t **in_bufs, value_t **out_bufs, size_t in_samples)
{
    Exchange_n(in_bufs, out_bufs, in_samples);

    if (m_silence >= in_samples) {
        m_silence -= in_samples;
        return in_samples;
    } else {
        size_t returned_silence = m_silence;
        m_silence = 0;
        return returned_silence;
    }
}

void VolumeLeveler::Exchange_n(value_t **in_bufs, value_t **out_bufs, size_t in_samples)
{
    // for each user_pos in user_buf
    for (size_t user_pos = 0; user_pos < in_samples; ++user_pos) {
        // compute multiplier
        value_t multiplier = (value_t)pow(m_avg_amp, -m_strength);
        if (multiplier > m_max_multiplier) multiplier = m_max_multiplier;

        // swap buf[pos] with user_buf[user_pos], scaling user[buf] by
        // multiplier and finding max of the new sample
        value_t new_val = 0;
        for (size_t ch = 0; ch < m_channels; ++ch) {
            value_t in = in_bufs[ch][user_pos];
            out_bufs[ch][user_pos] = m_bufs[ch][m_pos] * multiplier;
            m_bufs[ch][m_pos] = in;
            if (VLEVEL_ABS(in) > new_val) new_val = (value_t)fabs(in);
        }

        m_pos = (m_pos + 1) % m_samples; // now pos is the oldest, new one is pos-1

        m_avg_amp += m_max_slope;

        if (m_pos == m_max_slope_pos) {
            // recompute (this is expensive)
            m_max_slope = -HUGE_VAL;
            for (size_t i = 1; i < m_samples; ++i) {
                value_t sample_val = 0;
                for (size_t ch = 0; ch < m_channels; ++ch) {
                    value_t ch_val = VLEVEL_ABS(m_bufs[ch][(m_pos + i) % m_samples]);
                    if (ch_val > sample_val) sample_val = ch_val;
                }
                value_t slope = (sample_val - m_avg_amp) / i;
                if (slope >= m_max_slope) { // must be >=, otherwise clipping causes excessive computation
                    m_max_slope_pos = (m_pos + i) % m_samples;
                    m_max_slope = slope;
                    m_max_slope_val = sample_val;
                }
            }
        } else {
            // only chance of higher slope is the new sample
            // recomputing max_slope isn't really necessary...
            m_max_slope = (m_max_slope_val - m_avg_amp) / ((m_max_slope_pos - m_pos + m_samples) % m_samples);
            // ...but it doesn't take long and has a small effect.

            value_t slope = (new_val - m_avg_amp) / (m_samples - 1);

            if (slope >= m_max_slope) { // probably needs to be >= for same reason as above
                m_max_slope_pos = (m_pos - 1) % m_samples;
                m_max_slope = slope;
                m_max_slope_val = new_val;
            }
        }
    }
}
