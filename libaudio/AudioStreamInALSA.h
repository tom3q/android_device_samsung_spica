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

#ifndef _AUDIOSTREAMINALSA_H_
#define _AUDIOSTREAMINALSA_H_

#include "config.h"
#include <hardware_legacy/AudioHardwareBase.h>
#include "BufferProvider.h"
#include "AudioHardwareASoC.h"

extern "C" {
	struct pcm;
};

namespace android {

class BufferProvider;
class DownSampler;
class ChannelMixer;

class AudioStreamInALSA : public AudioStreamIn,
					public BufferProvider, public RefBase
{
	Mutex mLock;

	AudioHardware *mHardware;
	struct pcm *mPcm;

	bool mStandby;
	uint32_t mDevices;
	uint32_t mInputChannels;
	uint32_t mChannels;
	uint32_t mInputChannelCount;
	uint32_t mChannelCount;
	uint32_t mSampleRate;
	size_t mBufferSize;
	BufferProvider *mInputProvider;
	DownSampler *mDownSampler;
	ChannelMixer *mChannelMixer;
	status_t mReadStatus;
	size_t mInPcmInBuf;
	int16_t *mPcmIn;
	int mStandbyCnt;
	bool mSleepReq;

	// trace driver operations for dump
	int mDriverOp;

	uint32_t getInputSampleRate(uint32_t sampleRate);
	uint32_t getInputRouteFromDevice(uint32_t device);

	inline uint32_t frameSize(void)
	{
		return mChannelCount*sizeof(int16_t);
	}

public:
	AudioStreamInALSA();
	virtual ~AudioStreamInALSA();

	virtual ssize_t read(void *buffer, ssize_t bytes);
	virtual status_t dump(int fd, const Vector<String16> &args);
	virtual status_t standby();
	virtual status_t setParameters(const String8 &keyValuePairs);
	virtual String8 getParameters(const String8 &keys);

	virtual size_t bufferSize() const
	{
		return mBufferSize;
	}

	virtual uint32_t channels() const
	{
		return mChannels;
	}

	virtual int format() const
	{
		return AUDIO_HW_IN_FORMAT;
	}

	virtual uint32_t sampleRate() const
	{
		return mSampleRate;
	}

	virtual unsigned int getInputFramesLost() const
	{
		return 0;
	}

	virtual status_t setGain(float gain)
	{
		return INVALID_OPERATION;
	}

	// BufferProvider
	virtual status_t getNextBuffer(BufferProvider::Buffer *buffer);

	bool checkStandby();
	status_t set(AudioHardware *hw, uint32_t devices, int *pFormat,
				uint32_t *pChannels, uint32_t *pRate,
				AudioSystem::audio_in_acoustics acoustics);
	int wakeUp_l(void);
	void doStandby_l();
	void close_l();
	status_t open_l();
	static size_t getBufferSize(uint32_t sampleRate, int channelCount);
	int prepareLock();
	void lock();
	void unlock();

	int standbyCnt()
	{
		return mStandbyCnt;
	}

	uint32_t device()
	{
		return mDevices;
	}
};

}; /* namespace android */

#endif /* _AUDIOSTREAMINALSA_H_ */
