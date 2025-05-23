// This file is part of VLevel, a dynamic volume normalizer.
//
// Copyright 2003 Tom Felker <tcfelker@mtco.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2.1 of
// the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA

// volumeleveler.cpp - defines the VolumeLeveler class

#include <sys/types.h>
#include <math.h>

#include "vlevel.h"
#include "volumeleveler.h"

using namespace std;

VolumeLeveler::VolumeLeveler(size_t l, size_t c, value_t s, value_t m) : bufs(NULL)
{
	SetSamplesAndChannels(l, c);
	SetStrength(s);
	SetMaxMultiplier(m);
}

VolumeLeveler::~VolumeLeveler()
{
	if(bufs) {
		for(size_t ch = 0; ch < channels; ++ch)
			delete[] bufs[ch];
		delete[] bufs;
	}
}

void VolumeLeveler::SetStrength(value_t s)
{
	strength = s;
}

void VolumeLeveler::SetMaxMultiplier(value_t m)
{
	if(m <= 0) m = HUGE_VAL;
	max_multiplier = m;
}

void VolumeLeveler::SetSamplesAndChannels(size_t s, size_t c)
{
	if(bufs) {
		for(size_t ch = 0; ch < channels; ++ch)
			delete[] bufs[ch];
		delete[] bufs;
		bufs = NULL;
	}

	if(c > 0) {
		bufs = new value_t*[c];
		for(size_t ch = 0; ch < c; ++ch)
			bufs[ch] = new value_t[s];
	}

	samples = s;
	channels = c;
	Flush();
}

void VolumeLeveler::Flush()
{
	if(bufs) {
		for(size_t ch = 0; ch < channels; ++ch) {
			for(size_t i = 0; i < samples; ++i) {
				bufs[ch][i] = 0;
			}
		}
	}

	silence = samples;
	pos = max_slope_pos = 0;
	max_slope = max_slope_val = avg_amp = 0;
}

value_t VolumeLeveler::GetMultiplier()
{
	value_t multiplier = VLEVEL_POW(avg_amp, -strength);
	if(multiplier > max_multiplier) multiplier = max_multiplier;
	return multiplier;
}

template <typename BUF , value_t&(*index)(BUF bufs,size_t ch,size_t max_ch,size_t pos)>
size_t VolumeLeveler::Exchange(BUF in_bufs, BUF out_bufs, size_t in_samples)
{
	Exchange_n<BUF,index>(in_bufs, out_bufs, in_samples);

	if(silence >= in_samples) {
		silence -= in_samples;
		return in_samples;
	} else {
		size_t returned_silence = silence;
		silence = 0;
		return returned_silence;
	}
}

template <typename BUF , value_t&(*index)(BUF bufs,size_t ch,size_t max_ch,size_t pos)>
void VolumeLeveler::Exchange_n(BUF in_bufs, BUF out_bufs, size_t in_samples)
{
	// for each user_pos in user_buf
	for (size_t user_pos = 0; user_pos < in_samples; ++user_pos) {
		// compute multiplier
		value_t multiplier = VLEVEL_POW(avg_amp, -strength);

		// if avg_amp <= 0, then the above line sets multiplier to Inf, so
		// samples will scale to Inf or NaN.  This causes a tick on the
		// first sample after a Flush() unless max_multiplier is not Inf.
		// hopefully this fix isn't too slow.
		if(avg_amp <= 0) multiplier = 0;

		// untested!
		// The advantage of using floats is that you can be
		// sloppy with going over 1.  Since we have this nifty
		// average_amp calculation, let's apply it to limit
		// the audio to varying normally below 1.  Again,
		// hopefully this won't slow things down too much.
		if(avg_amp > 1) multiplier = 1 / avg_amp;

		// limit multiplier to max_multiplier.  max_multiplier can be Inf
		// to disable this.
		if(multiplier > max_multiplier) multiplier = max_multiplier;

		// swap buf[pos] with user_buf[user_pos], scaling user[buf] by
		// multiplier and finding max of the new sample
		value_t new_val = 0;
		for(size_t ch = 0; ch < channels; ++ch) {
			value_t in = index(in_bufs,ch,channels,user_pos);
			index(out_bufs,ch,channels,user_pos) = bufs[ch][pos] * multiplier;
			bufs[ch][pos] = in;
			if(VLEVEL_ABS(in) > new_val) new_val = VLEVEL_ABS(in);
		}

		pos = (pos + 1) % samples; // now pos is the oldest, new one is pos-1

		avg_amp += max_slope;

		if(pos == max_slope_pos) {
			// recompute (this is expensive)
			max_slope = -HUGE_VAL;
			for(size_t i = 1; i < samples; ++i) {
				value_t sample_val = 0;
				for(size_t ch = 0; ch < channels; ++ch) {
					value_t ch_val = VLEVEL_ABS(bufs[ch][(pos + i) % samples]);
					if(ch_val > sample_val) sample_val = ch_val;
				}
				value_t slope = (sample_val - avg_amp) / i;
				// must be >=, otherwise clipping causes excessive computation
				// TODO: maybe optimize - just save i, then compute slope, pos, and val only once later.
				if(slope >= max_slope) {
					max_slope_pos = (pos + i) % samples;
					max_slope = slope;
					max_slope_val = sample_val;
				}
			}
		} else {
			// only chance of higher slope is the new sample
			// recomputing max_slope isn't really necessary...
			max_slope = (max_slope_val - avg_amp) / ((max_slope_pos - pos + samples) % samples);
			// ...but it doesn't take long and has a small effect.

			value_t slope = (new_val - avg_amp) / (samples - 1);

			// probably needs to be >= for same reason as above
			if(slope >= max_slope) {
				max_slope_pos = (pos - 1) % samples;
				max_slope = slope;
				max_slope_val = new_val;
			}
		}
	}
}

value_t& bufferExchangePtrPtrIndex(value_t **bufs,size_t ch,size_t max_ch,size_t pos){return bufs[ch][pos];}
template size_t VolumeLeveler::Exchange<value_t **,bufferExchangePtrPtrIndex>(value_t **in_bufs, value_t **out_bufs, size_t in_samples);
template void VolumeLeveler::Exchange_n<value_t **,bufferExchangePtrPtrIndex>(value_t **in_bufs, value_t **out_bufs, size_t in_samples);

value_t& bufferExchangeInterleavedIndex(value_t *buf,size_t ch,size_t max_ch,size_t pos){return buf[pos * max_ch + ch];}
template size_t VolumeLeveler::Exchange<value_t *,bufferExchangeInterleavedIndex>(value_t *in_bufs, value_t *out_bufs, size_t in_samples);
template void VolumeLeveler::Exchange_n<value_t *,bufferExchangeInterleavedIndex>(value_t *in_bufs, value_t *out_bufs, size_t in_samples);
