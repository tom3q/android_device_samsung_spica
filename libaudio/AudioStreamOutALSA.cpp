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
#define LOG_TAG "AudioStreamOutALSA"

#include <cutils/log.h>
#include <hardware_legacy/power.h>
#include "AudioStreamOutALSA.h"
#include "AudioHardwareASoC.h"
#include "AudioStreamInALSA.h"
#include "AudioRouter.h"
#include "utils.h"

extern "C" {
#include "alsa_audio.h"
};

namespace android {

/*
 * AudioStreamOutALSA
 */

AudioStreamOutALSA::AudioStreamOutALSA() :
	mHardware(0),
	mPcm(0),
	mStandby(true),
	mDevices(0),
	mChannels(AUDIO_HW_OUT_CHANNELS),
	mSampleRate(AUDIO_HW_OUT_SAMPLERATE),
	mBufferSize(AUDIO_HW_OUT_PERIOD_BYTES),
#ifdef DRIVER_TRACE
	mDriverOp(DRV_NONE),
#endif
	mStandbyCnt(0),
	mSleepReq(false)
{
	TRACE();
}

status_t AudioStreamOutALSA::set(AudioHardware *hw,
			uint32_t devices, int *pFormat, uint32_t *pChannels,
			uint32_t *pRate)
{
	TRACE();
	int lFormat = format();
	uint32_t lChannels = channels();
	uint32_t lRate = sampleRate();

	mHardware = hw;
	mDevices = devices;

	if ((pFormat && *pFormat && *pFormat != lFormat)
	    || (pChannels && *pChannels && *pChannels != lChannels)
	    || (pRate && *pRate && *pRate != lRate)) {
		if (pFormat)
			*pFormat = lFormat;

		if (pChannels)
			*pChannels = lChannels;

		if (pRate)
			*pRate = lRate;

		return BAD_VALUE;
	}

	if (pFormat)
		*pFormat = lFormat;

	if (pChannels)
		*pChannels = lChannels;

	if (pRate)
		*pRate = lRate;

	mChannels = lChannels;
	mSampleRate = lRate;
	mBufferSize = AUDIO_HW_OUT_PERIOD_BYTES;

	return NO_ERROR;
}

AudioStreamOutALSA::~AudioStreamOutALSA()
{
	TRACE();
	standby();
}

uint32_t AudioStreamOutALSA::getOutputRouteFromDevice(uint32_t device)
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

	case (AudioSystem::DEVICE_OUT_SPEAKER |
				AudioSystem::DEVICE_OUT_WIRED_HEADPHONE):
	case (AudioSystem::DEVICE_OUT_SPEAKER |
				AudioSystem::DEVICE_OUT_WIRED_HEADSET):
		return BIT(AudioRouter::ENDPOINT_AMP) |
				BIT(AudioRouter::ENDPOINT_SPK) |
				BIT(AudioRouter::ENDPOINT_HP);

	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
	case AudioSystem::DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
		return BIT(AudioRouter::ENDPOINT_BT);

	default:
		return 0;
	}
}

int AudioStreamOutALSA::wakeUp_l(void)
{
	TRACE_VERBOSE();

	if (!mStandby)
		return 0;

	AutoMutex hwLock(mHardware->lock());

	LOGD("AudioHardware pcm playback is exiting standby.");
	acquire_wake_lock (PARTIAL_WAKE_LOCK, "AudioOutLock");

	sp<AudioStreamInALSA> spIn = mHardware->getInput();

	while (spIn != 0) {
		int cnt = spIn->prepareLock();
		mHardware->lock().unlock();
		// Mutex acquisition order is always out -> in -> hw
		spIn->lock();
		mHardware->lock().lock();

		// make sure that another thread did not change input state
		// while the mutex is released
		if ((spIn == mHardware->getInput()) &&
			(cnt == spIn->standbyCnt())) {
			LOGV("AudioStreamOutALSA::write() force input standby");
			spIn->close_l();
			break;
		}

		spIn->unlock();
		spIn = mHardware->getInput();
	}

	// spIn is not 0 here only if the input was active and has been
	// closed above

	// open output before input
	open_l();

	if (spIn != 0) {
		if (spIn->open_l() != NO_ERROR) {
			spIn->doStandby_l();
		}

		spIn->unlock();
	}

	if (!mPcm) {
		release_wake_lock("AudioOutLock");
		return -1;
	}

	mStandby = false;

	return 0;
}

ssize_t AudioStreamOutALSA::write(const void *buffer, size_t bytes)
{
	TRACE_VERBOSE();
	//LOGV("AudioStreamOutALSA::write(%p, %u)", buffer, bytes);

	status_t status = NO_INIT;
	const uint8_t *p = static_cast<const uint8_t *>(buffer);
	int ret;

	if (!mHardware)
		return NO_INIT;

	if (mSleepReq) {
		// 10ms are always shorter than the time to reconfigure
		// the audio path which is the only condition when mSleepReq
		// would be true.
		usleep(10000);
	}

	mLock.lock();

	if (wakeUp_l())
		goto error;

	TRACE_DRIVER_IN(DRV_PCM_WRITE)
	ret = pcm_write(mPcm,(void *) p, bytes);
	TRACE_DRIVER_OUT

	if (ret == 0) {
		mLock.unlock();
		return bytes;
	}

	LOGE("write error: %d", errno);

	status = -errno;

error:
	mLock.unlock();
	standby();

	// Simulate audio output timing in case of error
	usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());

	return status;
}

status_t AudioStreamOutALSA::standby()
{
	TRACE();

	if (!mHardware)
		return NO_INIT;

	mSleepReq = true;
	mLock.lock();
	mSleepReq = false;
	mHardware->lock().lock();
	doStandby_l();
	mHardware->lock().unlock();
	mLock.unlock();

	return NO_ERROR;
}

void AudioStreamOutALSA::doStandby_l()
{
	TRACE();
	++mStandbyCnt;

	if (!mStandby) {
		LOGD("AudioHardware pcm playback is going to standby.");
		release_wake_lock("AudioOutLock");
		mStandby = true;
	}

	close_l();
}

void AudioStreamOutALSA::close_l()
{
	TRACE();

	mHardware->setAudioRoute(AudioRouter::ROUTE_OUTPUT, 0);

	if (mPcm) {
		TRACE_DRIVER_IN(DRV_PCM_CLOSE)
		pcm_close(mPcm);
		TRACE_DRIVER_OUT
		mPcm = 0;
	}
}

status_t AudioStreamOutALSA::open_l()
{
	TRACE();
	unsigned int flags;
	flags = PCM_OUT
		| (AUDIO_HW_OUT_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT
		|(AUDIO_HW_OUT_PERIOD_CNT - PCM_PERIOD_CNT_MIN)
		<< PCM_PERIOD_CNT_SHIFT;

	LOGV("open pcm_out driver");

	TRACE_DRIVER_IN(DRV_PCM_OPEN)
	mPcm = pcm_open(flags);
	TRACE_DRIVER_OUT

	if (!mPcm) {
		LOGE("cannot open pcm_in driver: %s\n", strerror(errno));
		return NO_INIT;
	}

	if (!pcm_ready(mPcm)) {
		LOGE("PCM in not ready: %s\n", pcm_error(mPcm));
		TRACE_DRIVER_IN(DRV_PCM_CLOSE)
		pcm_close(mPcm);
		TRACE_DRIVER_OUT
		mPcm = 0;
		return NO_INIT;
	}

	if (mHardware->mode() != AudioSystem::MODE_IN_CALL) {
		uint32_t route = getOutputRouteFromDevice(mDevices);
		LOGV("write() wakeup setting route %d", route);
		mHardware->setAudioRoute(AudioRouter::ROUTE_OUTPUT, route);
	}

	return NO_ERROR;
}

status_t AudioStreamOutALSA::dump(int fd, const Vector<String16> &args)
{
	TRACE();
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;

	bool locked = mLock.tryLock();

	if (!locked) {
		snprintf(buffer, SIZE,
			 "\n\t\tAudioStreamOutALSA maybe deadlocked\n");
	} else {
		mLock.unlock();
	}

	snprintf(buffer, SIZE, "\t\tmHardware: %p\n", mHardware);
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tmPcm: %p\n", mPcm);
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tStandby %s\n", (mStandby) ? "ON" : "OFF");
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tmDevices: 0x%08x\n", mDevices);
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tmChannels: 0x%08x\n", mChannels);
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tmSampleRate: %d\n", mSampleRate);
	result.append(buffer);
	snprintf(buffer, SIZE, "\t\tmBufferSize: %d\n", mBufferSize);
	result.append(buffer);
#ifdef DRIVER_TRACE
	snprintf(buffer, SIZE, "\t\tmDriverOp: %d\n", mDriverOp);
	result.append(buffer);
#endif

	::write(fd, result.string(), result.size());

	return NO_ERROR;
}

bool AudioStreamOutALSA::checkStandby()
{
	TRACE();
	return mStandby;
}

status_t AudioStreamOutALSA::setParameters(const String8 &keyValuePairs)
{
	TRACE();
	AudioParameter param = AudioParameter(keyValuePairs);
	status_t status = NO_ERROR;
	int device;
	int ret;

	LOGD("AudioStreamOutALSA::setParameters() %s", keyValuePairs.string());

	if (!mHardware)
		return NO_INIT;

	mSleepReq = true;
	mLock.lock();
	mSleepReq = false;

	ret = param.getInt(String8(AudioParameter::keyRouting), device);

	if (ret == NO_ERROR) {
		if (device) {
			AutoMutex hwLock(mHardware->lock());

			if (mDevices != (uint32_t)device
			    && mHardware->mode() != AudioSystem::MODE_IN_CALL)
				doStandby_l();

			mDevices = (uint32_t)device;

			if (mHardware->mode() == AudioSystem::MODE_IN_CALL)
				mHardware->setIncallPath(device);
		}

		param.remove(String8(AudioParameter::keyRouting));
	}

	mLock.unlock();

	if (param.size())
		status = BAD_VALUE;

	return status;

}

String8 AudioStreamOutALSA::getParameters(const String8 &keys)
{
	TRACE();
	AudioParameter param = AudioParameter(keys);
	String8 value;
	String8 key = String8(AudioParameter::keyRouting);

	if (param.get(key, value) == NO_ERROR)
		param.addInt(key, (int)mDevices);

	LOGV("AudioStreamOutALSA::getParameters() %s",
		param.toString().string());

	return param.toString();
}

status_t AudioStreamOutALSA::getRenderPosition(
	uint32_t *dspFrames)
{
	TRACE();
	//TODO
	return INVALID_OPERATION;
}

int AudioStreamOutALSA::prepareLock()
{
	TRACE();
	// request sleep next time write() is called so that caller can acquire
	// mLock
	mSleepReq = true;
	return mStandbyCnt;
}

void AudioStreamOutALSA::lock()
{
	TRACE();
	mLock.lock();
	mSleepReq = false;
}

void AudioStreamOutALSA::unlock()
{
	TRACE();
	mLock.unlock();
}

}; /* namespace android */
