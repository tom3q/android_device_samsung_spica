/*
 * Copyright 2012, The Android Open-Source Project
 * Copyright 2012, Tomasz Figa <tomasz.figa at gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_NDEBUG 0
#define LOG_TAG "DownSampler"

#include <cutils/log.h>
#include <utils/Errors.h>
#include "DownSampler.h"
#include "utils.h"

/*
 * DownSampler
 */

/*
 * 2.30 fixed point FIR filter coefficients for conversion 44100 -> 22050.
 * (Works equivalently for 22010 -> 11025 or any other halving, of course.)
 *
 * Transition band from about 18 kHz, passband ripple < 0.1 dB,
 * stopband ripple at about -55 dB, linear phase.
 *
 * Design and display in MATLAB or Octave using:
 *
 * filter = fir1(19, 0.5); filter = round(filter * 2**30);
 * freqz(filter * 2**-30);
 */
static const int32_t filter_22khz_coeff[] = {
	2089257, 2898328, -5820678, -10484531,
	19038724, 30542725, -50469415, -81505260,
	152544464, 478517512, 478517512, 152544464,
	-81505260, -50469415, 30542725, 19038724,
	-10484531, -5820678, 2898328, 2089257,
};
#define NUM_COEFF_22KHZ		(NELEM(filter_22khz_coeff))
#define OVERLAP_22KHZ		(NUM_COEFF_22KHZ - 2)

/*
 * Convolution of signals A and reverse(B). (In our case, the filter response
 * is symmetric, so the reversing doesn't matter.) A is taken to be in 0.16
 * fixed-point, and B is taken to be in2.30 fixed-point. The answer will be
 * in 16.16 fixed-point, unclipped.
 *
 * This function would probably be the prime candidate for SIMD conversion if
 * you want more speed.
 */
static int32_t fir_convolve(const int16_t *a, const int32_t *b,
						int num_samples, int skip)
{
	TRACE_VERBOSE();
	int32_t sum = 1 << 13;

	for (int i = 0; i < num_samples; ++i, a += skip, ++b)
		sum += a[0] * (b[0] >> 16);

	return sum >> 14;
}

/* Clip from 16.16 fixed-point to 0.16 fixed-point. */
static int16_t clip(int32_t x)
{
	TRACE_VERBOSE();

	if (x < -32768)
		x = -32768;

	if (x > 32767)
		x = 32767;

	return x;
}

/*
 * Convert a chunk from 44 kHz to 22 kHz. Will update num_samples_in and
 * num_samples_out accordingly, since it may leave input samples in the buffer
 * due to overlap.
 *
 * Input and output are taken to be in 0.16 fixed-point.
 */
static int resample_2_1(int16_t *input, int16_t *output,
						int *num_samples_in, int skip)
{
	TRACE_VERBOSE();

	if (*num_samples_in < (int)NUM_COEFF_22KHZ)
		return 0;

	int odd_smp = *num_samples_in & 0x1;
	int num_samples = *num_samples_in - odd_smp - OVERLAP_22KHZ;
	int16_t *in_ptr = input;

	for (int i = 0; i < num_samples; i += 2) {
		*output = clip(fir_convolve(in_ptr,
				filter_22khz_coeff, NUM_COEFF_22KHZ, skip));
		in_ptr += 2*skip;
		output += skip;
	}

	*num_samples_in = OVERLAP_22KHZ + odd_smp;
	return num_samples / 2;
}

/*
 * 2.30 fixed point FIR filter coefficients for conversion 22050 -> 16000,
 * or 11025 -> 8000.
 *
 * Transition band from about 14 kHz, passband ripple < 0.1 dB,
 * stopband ripple at about -50 dB, linear phase.
 *
 * Design and display in MATLAB or Octave using:
 *
 * filter = fir1(23, 16000 / 22050); filter = round(filter * 2**30);
 * freqz(filter * 2**-30);
 */
static const int32_t filter_16khz_coeff[] = {
	2057290, -2973608, 1880478, 4362037,
	-14639744, 18523609, -1609189, -38502470,
	78073125, -68353935, -59103896, 617555440,
	617555440, -59103896, -68353935, 78073125,
	-38502470, -1609189, 18523609, -14639744,
	4362037, 1880478, -2973608, 2057290,
};
#define NUM_COEFF_16KHZ (NELEM(filter_16khz_coeff))
#define OVERLAP_16KHZ (NUM_COEFF_16KHZ - 1)

/*
 * Convert a chunk from 22 kHz to 16 kHz. Will update num_samples_in and
 * num_samples_out accordingly, since it may leave input samples in the buffer
 * due to overlap.
 *
 * This implementation is rather ad-hoc; it first low-pass filters the data
 * into a temporary buffer, and then converts chunks of 441 input samples at a
 * time into 320 output samples by simple linear interpolation. A better
 * implementation would use a polyphase filter bank to do these two operations
 * in one step.
 *
 * Input and output are taken to be in 0.16 fixed-point.
 */

#define RESAMPLE_16KHZ_SAMPLES_IN 441
#define RESAMPLE_16KHZ_SAMPLES_OUT 320

static int resample_441_320(int16_t *input, int16_t *output,
						int *num_samples_in, int skip)
{
	TRACE_VERBOSE();
	const int num_blocks = (*num_samples_in - OVERLAP_16KHZ)
						/ RESAMPLE_16KHZ_SAMPLES_IN;

	if (num_blocks < 1)
		return 0;

	int16_t *in_ptr = input;

	for (int i = 0; i < num_blocks; ++i) {
		uint32_t tmp[RESAMPLE_16KHZ_SAMPLES_IN];
		const int16_t *ptr = in_ptr;

		for (int j = 0; j < RESAMPLE_16KHZ_SAMPLES_IN; ++j, ptr += skip)
			tmp[j] = fir_convolve(ptr,
				filter_16khz_coeff, NUM_COEFF_16KHZ, skip);

		const float step_float = (float)RESAMPLE_16KHZ_SAMPLES_IN
					 / (float)RESAMPLE_16KHZ_SAMPLES_OUT;
		const uint32_t step = (uint32_t)(step_float * 32768.0f + 0.5f);
		uint32_t in_sample_num = 0;		// 17.15 fixed point

		for (int j = 0; j < RESAMPLE_16KHZ_SAMPLES_OUT; ++j) {
			const uint32_t whole = in_sample_num >> 15;
			const uint32_t frac = (in_sample_num & 0x7fff);
			const int32_t s1 = tmp[whole];	// 0.15 fixed point
			const int32_t s2 = tmp[whole + 1];
			*output = clip(s1
					+ (((s2 - s1) * (int32_t)frac) >> 15));
			output += skip;
			in_sample_num += step;
		}

		in_ptr += skip*RESAMPLE_16KHZ_SAMPLES_IN;
	}

	const int samples_consumed = num_blocks * RESAMPLE_16KHZ_SAMPLES_IN;
	*num_samples_in -= samples_consumed;
	return RESAMPLE_16KHZ_SAMPLES_OUT * num_blocks;
}

namespace android {

DownSampler::DownSampler(uint32_t outSampleRate,
				uint32_t channelCount, uint32_t frameCount,
				BufferProvider *provider) :
	mStatus(NO_INIT),
	mProvider(provider),
	mSampleRate(outSampleRate),
	mChannelCount(channelCount),
	mFrameCount(frameCount),
	mOutBufIdx(0)
{
	TRACE();
	LOGD("DownSampler() cstor %p SR %d channels %d "
		"frames %d", this, mSampleRate, mChannelCount, mFrameCount);

	for (unsigned int i = 0; i < NELEM(mTmpBuf); ++i)
		mTmpBuf[i] = 0;

	if (mSampleRate != 8000 && mSampleRate != 11025
		&& mSampleRate != 16000 && mSampleRate != 22050) {
		LOGE("DownSampler cstor: "
			"bad sampling rate: %d", mSampleRate);
		return;
	}

	for (unsigned int i = 0; i < NELEM(mTmpBuf); ++i) {
		mTmpBuf[i] = new int16_t[channelCount*mFrameCount];

		if (!mTmpBuf[i]) {
			LOGE("DownSampler: Failed to allocate "
				"work buffer %d/%d", i, NELEM(mTmpBuf));
			return;
		}

		mInTmpBuf[i] = 0;
	}

	mStatus = NO_ERROR;
}

DownSampler::~DownSampler()
{
	TRACE();

	for (unsigned int i = 0; i < NELEM(mTmpBuf); ++i)
		if (mTmpBuf[i])
			delete[] mTmpBuf[i];
}

void DownSampler::reset()
{
	TRACE();

	for (unsigned int i = 0; i < NELEM(mInTmpBuf); ++i)
		mInTmpBuf[i] = 0;
}

static inline void copySamples(int16_t *dst, const int16_t *src, size_t cnt)
{
	memcpy(dst, src, cnt*sizeof(*dst));
}

static inline void moveSamples(int16_t *dst, const int16_t *src, size_t cnt)
{
	memmove(dst, src, cnt*sizeof(*dst));
}

status_t DownSampler::getNextBuffer(BufferProvider::Buffer *buffer)
{
	TRACE_VERBOSE();

	if (mStatus != NO_ERROR)
		return mStatus;

	if (!buffer || !buffer->raw || !buffer->frameCount)
		return BAD_VALUE;

	int outFrames = 0;
	int remaingFrames = buffer->frameCount;
	int16_t *out = buffer->i16;

	int inOutBuf = mInTmpBuf[mOutBufIdx];

	if (inOutBuf) {
		if (remaingFrames < inOutBuf)
			inOutBuf = remaingFrames;

		copySamples(out, mTmpBuf[mOutBufIdx], inOutBuf*mChannelCount);
		remaingFrames -= inOutBuf;
		outFrames += inOutBuf;

		if (inOutBuf != mInTmpBuf[mOutBufIdx])
			moveSamples(mTmpBuf[mOutBufIdx], mTmpBuf[mOutBufIdx]
					+ inOutBuf*mChannelCount,
					(mInTmpBuf[mOutBufIdx] - inOutBuf)
					*mChannelCount);

		mInTmpBuf[mOutBufIdx] -= inOutBuf;
		out += inOutBuf*mChannelCount;
	}

	while (remaingFrames) {
		unsigned int bufIdx = 0;
		unsigned int sampleRate = 44100;
		BufferProvider::Buffer buf;
		int ret;

		buf.raw = mTmpBuf[bufIdx] + mInTmpBuf[bufIdx]*mChannelCount;
		buf.frameCount = mFrameCount - mInTmpBuf[bufIdx];

		ret = mProvider->getNextBuffer(&buf);

		if (ret) {
			buffer->frameCount = outFrames;
			return ret;
		}

		mInTmpBuf[bufIdx] += buf.frameCount;

		while (sampleRate > mSampleRate) {
			int samplesIn = mInTmpBuf[bufIdx];
			int samplesOut;
			int16_t *inBuf = mTmpBuf[bufIdx];
			int16_t *outBuf = mTmpBuf[bufIdx + 1]
					+ mInTmpBuf[bufIdx + 1]*mChannelCount;

			if (2*mSampleRate <= sampleRate) {
				samplesOut = resample_2_1(inBuf, outBuf,
						&samplesIn, mChannelCount);

				if (mChannelCount == 2) {
					samplesIn = mInTmpBuf[bufIdx];
					resample_2_1(inBuf + 1, outBuf + 1,
						&samplesIn, mChannelCount);
				}

				sampleRate /= 2;
			} else {
				samplesOut = resample_441_320(inBuf, outBuf,
						&samplesIn, mChannelCount);

				if (mChannelCount == 2) {
					samplesIn = mInTmpBuf[bufIdx];
					resample_441_320(inBuf + 1, outBuf + 1,
						&samplesIn, mChannelCount);
				}

				sampleRate *= 320;
				sampleRate /= 441;
			}

			if (samplesIn && samplesIn != mInTmpBuf[bufIdx])
				moveSamples(inBuf, inBuf + (mInTmpBuf[bufIdx]
						- samplesIn)*mChannelCount,
						samplesIn*mChannelCount);

			mInTmpBuf[bufIdx] = samplesIn;
			mInTmpBuf[++bufIdx] += samplesOut;
		}

		int frames = (remaingFrames > mInTmpBuf[bufIdx]) ?
					mInTmpBuf[bufIdx] : remaingFrames;
		copySamples(out, mTmpBuf[bufIdx], frames*mChannelCount);

		if (mInTmpBuf[bufIdx] > remaingFrames)
			moveSamples(mTmpBuf[bufIdx], mTmpBuf[bufIdx]
				+ frames*mChannelCount, (mInTmpBuf[bufIdx]
				- remaingFrames)*mChannelCount);

		outFrames += frames;
		remaingFrames -= frames;
		mInTmpBuf[bufIdx] -= frames;
		out += frames*mChannelCount;
		mOutBufIdx = bufIdx;
	}

	return 0;
}

}; /* namespace android */
