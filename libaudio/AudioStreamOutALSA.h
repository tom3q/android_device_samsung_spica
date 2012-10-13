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

#ifndef _AUDIOSTREAMOUTALSA_H_
#define _AUDIOSTREAMOUTALSA_H_

#include "config.h"

#include <stdint.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <utils/RefBase.h>
#include <hardware_legacy/AudioHardwareBase.h>

extern "C" {
	struct pcm;
};

namespace android {

class AudioHardware;

class AudioStreamOutALSA : public AudioStreamOut, public RefBase {
	Mutex mLock;

	AudioHardware *mHardware;
	struct pcm *mPcm;

	bool mStandby;
	uint32_t mDevices;
	uint32_t mChannels;
	uint32_t mSampleRate;
	size_t mBufferSize;
	int mStandbyCnt;
	bool mSleepReq;

	// trace driver operations for dump
	int mDriverOp;

	uint32_t getOutputRouteFromDevice(uint32_t device);

public:
	AudioStreamOutALSA();
	virtual ~AudioStreamOutALSA();


	virtual ssize_t write(const void *buffer, size_t bytes);
	virtual status_t standby();
	virtual status_t dump(int fd, const Vector<String16> &args);
	virtual status_t setParameters(const String8 &keyValuePairs);
	virtual String8 getParameters(const String8 &keys);
	virtual status_t getRenderPosition(uint32_t *dspFrames);

	virtual uint32_t sampleRate() const {
		return mSampleRate;
	}

	virtual size_t bufferSize() const {
		return mBufferSize;
	}

	virtual uint32_t channels() const {
		return mChannels;
	}

	virtual int format() const {
		return AUDIO_HW_OUT_FORMAT;
	}

	virtual uint32_t latency() const {
		return (1000 * AUDIO_HW_OUT_PERIOD_CNT *
				(bufferSize()/frameSize()))/sampleRate() +
				AUDIO_HW_OUT_LATENCY_MS;
	}

	virtual status_t setVolume(float left, float right) {
		return INVALID_OPERATION;
	}

	bool checkStandby();
	status_t set(AudioHardware *mHardware, uint32_t devices,
			int *pFormat, uint32_t *pChannels, uint32_t *pRate);
	int wakeUp_l(void);
	void doStandby_l();
	void close_l();
	status_t open_l();
	int prepareLock();
	void lock();
	void unlock();

	uint32_t device() {
		return mDevices;
	}

	int standbyCnt() {
		return mStandbyCnt;
	}
};

}; /* namespace android */

#endif /* _AUDIOSTREAMOUTALSA_H_ */
