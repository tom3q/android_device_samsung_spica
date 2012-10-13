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

#ifndef _AUDIO_HARDWARE_ASOC_H_
#define _AUDIO_HARDWARE_ASOC_H_

#include <stdint.h>
#include <sys/types.h>

#include <utils/threads.h>
#include <utils/SortedVector.h>

#include <hardware_legacy/AudioHardwareBase.h>
#include <media/mediarecorder.h>

#include "utils.h"
#include "config.h"
#include "AudioRouter.h"

namespace android {

class AudioStreamOutALSA;
class AudioStreamInALSA;

class AudioHardware : public AudioHardwareBase {
	Mutex mLock;

	sp<AudioStreamOutALSA> mOutput;
	SortedVector<sp<AudioStreamInALSA> > mInputs;
	sp<AudioRouter> mRouter;

	bool mMicMute;
	bool mInCallAudioMode;
	float mVoiceVolume;

	// trace driver operations for dump
	int mDriverOp;

	status_t mStatus;

	uint32_t getOutputRouteFromDevice(uint32_t device);
	uint32_t getInputRouteFromDevice(uint32_t device);
	uint32_t getVoiceOutRouteFromDevice(uint32_t device);
	uint32_t getVoiceInRouteFromDevice(uint32_t device);

protected:
	virtual status_t dump(int fd, const Vector<String16> &args);

public:
	AudioHardware();
	virtual ~AudioHardware();

	virtual status_t initCheck()
	{
		return mStatus;
	}

	virtual status_t setVoiceVolume(float volume);
	virtual status_t setMasterVolume(float volume);
	virtual status_t setMode(int mode);
	virtual status_t setMicMute(bool state);
	virtual status_t getMicMute(bool *state);
	virtual status_t setParameters(const String8 &keyValuePairs);
	virtual String8 getParameters(const String8 &keys);
	virtual size_t getInputBufferSize( uint32_t sampleRate,
						int format, int channelCount);
	virtual AudioStreamOut *openOutputStream(uint32_t devices,
			int *format = 0, uint32_t *channels = 0,
			uint32_t *sampleRate = 0, status_t *status = 0);
	virtual void closeOutputStream(AudioStreamOut *out);
	virtual AudioStreamIn *openInputStream(uint32_t devices,
				int *format, uint32_t *channels,
				uint32_t *sampleRate, status_t *status,
				AudioSystem::audio_in_acoustics acoustics);
	virtual void closeInputStream(AudioStreamIn *in);

	status_t setIncallPath(uint32_t device);
	status_t setOutputPath(uint32_t device);
	status_t setInputPath(uint32_t device);

	void setAudioRoute(AudioRouter::RouteType type, uint32_t route)
	{
		if (mRouter != NULL)
			mRouter->setAudioRoute(type, route);
	}

	sp <AudioStreamInALSA> getInput();

	sp <AudioStreamOutALSA> getOutput()
	{
		return mOutput;
	}

	int mode() const
	{
		return mMode;
	}

	Mutex &lock()
	{
		return mLock;
	}
};

}; /* namespace android */

#endif /* _AUDIO_HARDWARE_ASOC_H_ */
