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

#include <math.h>

#define LOG_NDEBUG 0
#define LOG_TAG "AudioHardware"

#include <utils/Log.h>
#include <utils/String8.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <dlfcn.h>
#include <fcntl.h>

#include <media/AudioRecord.h>
#include <hardware_legacy/power.h>

#include "AudioHardwareASoC.h"
#include "AudioStreamOutALSA.h"
#include "AudioStreamInALSA.h"
#include "AudioRouter.h"
#include "utils.h"

namespace android {

int Tracer::level;

/*
 * AudioHardware
 */

AudioHardware::AudioHardware() :
	mMicMute(false),
	mInCallAudioMode(false),
	mVoiceVolume(0.0f),
	mDriverOp(DRV_NONE),
	mStatus(NO_INIT)
{
	TRACE();

	mRouter = new AudioRouter();

	if (mRouter == 0 || mRouter->initCheck() != NO_ERROR) {
		mRouter.clear();
		LOGE("Failed to initialize AudioRouter");
		return;
	}

	mStatus = NO_ERROR;
}

AudioHardware::~AudioHardware()
{
	TRACE();

	for (size_t index = 0; index < mInputs.size(); index++)
		closeInputStream(mInputs[index].get());

	mInputs.clear();
	closeOutputStream((AudioStreamOut *)mOutput.get());
	mRouter.clear();

	mStatus = NO_INIT;
}

AudioStreamOut *AudioHardware::openOutputStream(
	uint32_t devices, int *format, uint32_t *channels,
	uint32_t *sampleRate, status_t *status)
{
	TRACE();
	sp <AudioStreamOutALSA> out;
	status_t rc;

	{
		// scope for the lock
		Mutex::Autolock lock(mLock);

		// only one output stream allowed
		if (mOutput != 0) {
			if (status)
				*status = INVALID_OPERATION;

			return 0;
		}

		out = new AudioStreamOutALSA();

		rc = out->set(this, devices, format, channels, sampleRate);

		if (rc == NO_ERROR)
			mOutput = out;
	}

	if (rc != NO_ERROR) {
		if (out != 0)
			out.clear();
	}

	if (status)
		*status = rc;

	return out.get();
}

void AudioHardware::closeOutputStream(AudioStreamOut *out)
{
	TRACE();
	sp <AudioStreamOutALSA> spOut;
	{
		Mutex::Autolock lock(mLock);

		if (mOutput == 0 || mOutput.get() != out) {
			LOGW("Attempt to close invalid output stream");
			return;
		}

		spOut = mOutput;
		mOutput.clear();
	}
	spOut.clear();
}

AudioStreamIn *AudioHardware::openInputStream(
	uint32_t devices, int *format, uint32_t *channels,
	uint32_t *sampleRate, status_t *status,
	AudioSystem::audio_in_acoustics acoustic_flags)
{
	TRACE();

	// check for valid input source
	if (!AudioSystem::isInputDevice((AudioSystem::audio_devices)devices)) {
		if (status)
			*status = BAD_VALUE;

		return 0;
	}

	status_t rc = NO_ERROR;
	sp <AudioStreamInALSA> in;

	{
		// scope for the lock
		Mutex::Autolock lock(mLock);

		in = new AudioStreamInALSA();
		rc = in->set(this, devices, format, channels,
						sampleRate, acoustic_flags);

		if (rc == NO_ERROR)
			mInputs.add(in);
	}

	if (rc != NO_ERROR) {
		if (in != 0)
			in.clear();
	}

	if (status)
		*status = rc;

	LOGV("AudioHardware::openInputStream()%p", in.get());
	return in.get();
}

void AudioHardware::closeInputStream(AudioStreamIn *in)
{
	TRACE();

	sp<AudioStreamInALSA> spIn;
	{
		Mutex::Autolock lock(mLock);

		ssize_t index = mInputs.indexOf((AudioStreamInALSA *)in);

		if (index < 0) {
			LOGW("Attempt to close invalid input stream");
			return;
		}

		spIn = mInputs[index];
		mInputs.removeAt(index);
	}
	LOGV("AudioHardware::closeInputStream()%p", in);
	spIn.clear();
}

status_t AudioHardware::setMode(int mode)
{
	TRACE();
	sp<AudioStreamOutALSA> spOut;
	sp<AudioStreamInALSA> spIn;
	status_t status;

	// Mutex acquisition order is always out -> in -> hw
	AutoMutex lock(mLock);

	spOut = mOutput;

	while (spOut != 0) {
		if (!spOut->checkStandby()) {
			int cnt = spOut->prepareLock();
			mLock.unlock();
			spOut->lock();
			mLock.lock();

			// make sure that another thread did not change
			// output state while the mutex is released
			if ((spOut == mOutput)
			    && (cnt == spOut->standbyCnt()))
				break;

			spOut->unlock();
			spOut = mOutput;
		} else
			spOut.clear();
	}

	// spOut is not 0 here only if the output is active

	spIn = getInput();

	while (spIn != 0) {
		int cnt = spIn->prepareLock();
		mLock.unlock();
		spIn->lock();
		mLock.lock();

		// make sure that another thread did not change
		// input state while the mutex is released
		if ((spIn == getInput())
		    && (cnt == spIn->standbyCnt()))
			break;

		spIn->unlock();
		spIn = getInput();
	}

	// spIn is not 0 here only if the input is active

	int prevMode = mMode;
	bool goingIntoInCall = (mode == AudioSystem::MODE_IN_CALL);

	LOGV("setMode() : new %d, old %d", mode, prevMode);
	status = AudioHardwareBase::setMode(mode);

	if (status != NO_ERROR)
		goto unlock;

	if (goingIntoInCall == mInCallAudioMode)
		goto unlock;

	if (goingIntoInCall) {
		if (spOut != 0) {
			LOGV("setMode() in call force output standby");
			spOut->doStandby_l();
		}

		if (spIn != 0) {
			LOGV("setMode() in call force input standby");
			spIn->doStandby_l();
		}

		mRouter->setRouteDisable(AudioRouter::ROUTE_INPUT, true);
		mRouter->setRouteDisable(AudioRouter::ROUTE_OUTPUT, true);
		mRouter->setVoiceVolume(mVoiceVolume);
		mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_IN,
					BIT(AudioRouter::ENDPOINT_MIC_MAIN));
		mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_OUT,
					BIT(AudioRouter::ENDPOINT_RCV));

		mInCallAudioMode = true;
	} else {
		mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_IN, 0);
		mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_OUT, 0);
		mRouter->setRouteDisable(AudioRouter::ROUTE_OUTPUT, false);
		mRouter->setRouteDisable(AudioRouter::ROUTE_INPUT, false);
		mRouter->setVoiceVolume(0.0f);

		if (spOut != 0) {
			LOGV("setMode() off call force output standby");
			spOut->doStandby_l();
		}

		if (spIn != 0) {
			LOGV("setMode() off call force input standby");
			spIn->doStandby_l();
		}

		mInCallAudioMode = false;
	}

unlock:

	if (spIn != 0)
		spIn->unlock();

	if (spOut != 0)
		spOut->unlock();

	return status;
}

status_t AudioHardware::setMicMute(bool state)
{
	TRACE();
	LOGV("setMicMute(%d) mMicMute %d", state, mMicMute);
	sp<AudioStreamInALSA> spIn;

	mLock.lock();

	mRouter->setRouteDisable(AudioRouter::ROUTE_VOICE_IN, state);
	mRouter->setRouteDisable(AudioRouter::ROUTE_INPUT, state);

	mMicMute = state;

	mLock.unlock();

	if (spIn != 0)
		spIn->standby();

	return NO_ERROR;
}

status_t AudioHardware::getMicMute(bool *state)
{
	TRACE();
	*state = mMicMute;
	return NO_ERROR;
}

status_t AudioHardware::setParameters(const String8 &keyValuePairs)
{
	TRACE();
	return NO_ERROR;
}

String8 AudioHardware::getParameters(const String8 &keys)
{
	TRACE();
	AudioParameter request = AudioParameter(keys);
	AudioParameter reply = AudioParameter();

	LOGV("getParameters() %s", keys.string());

	return reply.toString();
}

size_t AudioHardware::getInputBufferSize(uint32_t sampleRate,
		int format, int channelCount)
{
	TRACE();

	if (format != AudioSystem::PCM_16_BIT) {
		LOGW("getInputBufferSize bad format: %d", format);
		return 0;
	}

	if (channelCount < 1 || channelCount > 2) {
		LOGW("getInputBufferSize bad channel count: %d", channelCount);
		return 0;
	}

	switch (sampleRate) {
	case 8000:
	case 11025:
	case 16000:
	case 22050:
	case 44100:
		break;
	default:
		LOGE("getInputBufferSize bad sample rate: %d", sampleRate);
		return 0;
	}

	return AudioStreamInALSA::getBufferSize(sampleRate, channelCount);
}

status_t AudioHardware::setVoiceVolume(float volume)
{
	TRACE();
	AutoMutex lock(mLock);

	mVoiceVolume = volume;

	if (mInCallAudioMode)
		mRouter->setVoiceVolume(volume);

	return NO_ERROR;
}

status_t AudioHardware::setMasterVolume(float volume)
{
	TRACE();
	AutoMutex lock(mLock);

	mRouter->setMasterVolume(volume);

	return NO_ERROR;
}

static const int kDumpLockRetries = 50;
static const int kDumpLockSleep = 20000;

static bool tryLock(Mutex &mutex)
{
	TRACE();
	bool locked = false;

	for (int i = 0; i < kDumpLockRetries; ++i) {
		if (mutex.tryLock() == NO_ERROR) {
			locked = true;
			break;
		}

		usleep(kDumpLockSleep);
	}

	return locked;
}

status_t AudioHardware::dump(int fd, const Vector<String16> &args)
{
	TRACE();
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;

	bool locked = tryLock(mLock);

	if (!locked)
		snprintf(buffer, SIZE, "\n\tAudioHardware maybe deadlocked\n");
	else
		mLock.unlock();

	snprintf(buffer, SIZE, "\tInit %s\n", (mStatus == NO_ERROR)
		 ? "OK" : "Failed");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tMic Mute %s\n", (mMicMute) ? "ON" : "OFF");
	result.append(buffer);
	snprintf(buffer, SIZE, "\tIn Call Audio Mode %s\n",
		 (mInCallAudioMode) ? "ON" : "OFF");
	result.append(buffer);
#ifdef DRIVER_TRACE
	snprintf(buffer, SIZE, "\tmDriverOp: %d\n", mDriverOp);
	result.append(buffer);
#endif

	snprintf(buffer, SIZE, "\n\tmOutput %p dump:\n", mOutput.get());
	result.append(buffer);
	write(fd, result.string(), result.size());

	if (mOutput != 0)
		mOutput->dump(fd, args);

	snprintf(buffer, SIZE, "\n\t%d inputs opened:\n", mInputs.size());
	write(fd, buffer, strlen(buffer));

	for (size_t i = 0; i < mInputs.size(); i++) {
		snprintf(buffer, SIZE, "\t- input %d dump:\n", i);
		write(fd, buffer, strlen(buffer));
		mInputs[i]->dump(fd, args);
	}

	return NO_ERROR;
}

status_t AudioHardware::setIncallPath(uint32_t device)
{
	TRACE();
	LOGV("setIncallPath_l: device %x", device);

	if (mMode != AudioSystem::MODE_IN_CALL)
		return NO_ERROR;

	LOGD("### incall mode route (%d)", device);

	if (mRouter == NULL) {
		LOGW("Called setIncallPath_l in MODE_IN_CALL, "
						"but mRouter is NULL");
		return NO_ERROR;
	}

	uint32_t outRoute = getVoiceOutRouteFromDevice(device);
	uint32_t inRoute = getVoiceInRouteFromDevice(device);

	LOGV("setIncallPath_l() Voice Call Path, (%x)", device);

	mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_OUT, outRoute);
	mRouter->setAudioRoute(AudioRouter::ROUTE_VOICE_IN, inRoute);

	return NO_ERROR;
}

uint32_t AudioHardware::getVoiceOutRouteFromDevice(uint32_t device)
{
	TRACE();

	switch (device) {
	case AudioSystem::DEVICE_OUT_EARPIECE:
		return BIT(AudioRouter::ENDPOINT_RCV);

	case AudioSystem::DEVICE_OUT_SPEAKER:
		return BIT(AudioRouter::ENDPOINT_AMP) |
						BIT(AudioRouter::ENDPOINT_SPK);

	case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE:
	case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
		return BIT(AudioRouter::ENDPOINT_AMP) |
						BIT(AudioRouter::ENDPOINT_HP);

	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
		return BIT(AudioRouter::ENDPOINT_BT);

	default:
		return 0;
	}
}

uint32_t AudioHardware::getVoiceInRouteFromDevice(uint32_t device)
{
	TRACE();

	switch (device) {
	case AudioSystem::DEVICE_OUT_EARPIECE:
	case AudioSystem::DEVICE_OUT_WIRED_HEADPHONE:
		return BIT(AudioRouter::ENDPOINT_MIC_MAIN);

	case AudioSystem::DEVICE_OUT_SPEAKER:
		return BIT(AudioRouter::ENDPOINT_MIC_SUB);

	case AudioSystem::DEVICE_OUT_WIRED_HEADSET:
		return BIT(AudioRouter::ENDPOINT_MIC_HP);

	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
		return BIT(AudioRouter::ENDPOINT_MIC_BT);

	default:
		return 0;
	}
}

// getInput() must be called with mLock held
sp <AudioStreamInALSA> AudioHardware::getInput()
{
	TRACE();

	for (size_t i = 0; i < mInputs.size(); i++) {
		// return first input found not being in standby mode
		// as only one input can be in this state
		if (!mInputs[i]->checkStandby())
			return mInputs[i];
	}

	return 0;
}

/*
 * Factory
 */

extern "C" AudioHardwareInterface *createAudioHardware(void)
{
	TRACE();
	return new AudioHardware();
}

}; /* namespace android */