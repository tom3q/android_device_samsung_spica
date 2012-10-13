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
#define LOG_TAG "AudioStreamInALSA"

#include <cutils/log.h>
#include <hardware_legacy/power.h>
#include "AudioStreamInALSA.h"
#include "AudioHardwareASoC.h"
#include "AudioStreamOutALSA.h"
#include "AudioRouter.h"
#include "ChannelMixer.h"
#include "DownSampler.h"

extern "C" {
#include "alsa_audio.h"
};

namespace android {

/*
 * AudioStreamInALSA
 */

AudioStreamInALSA::AudioStreamInALSA() :
	mHardware(0),
	mPcm(0),
	mStandby(true),
	mDevices(0),
	mChannels(AUDIO_HW_IN_CHANNELS),
	mChannelCount(2),
	mSampleRate(AUDIO_HW_IN_SAMPLERATE),
	mBufferSize(AUDIO_HW_IN_PERIOD_BYTES),
	mDownSampler(0),
	mChannelMixer(0),
	mReadStatus(NO_ERROR),
	mInPcmInBuf(0),
	mPcmIn(0),
#ifdef DRIVER_TRACE
	mDriverOp(DRV_NONE),
#endif
	mStandbyCnt(0),
	mSleepReq(false)
{
	TRACE();
}

status_t AudioStreamInALSA::set(AudioHardware *hw,
		uint32_t devices, int *pFormat, uint32_t *pChannels,
		uint32_t *pRate, AudioSystem::audio_in_acoustics acoustics)
{
	TRACE();

	if (!pFormat || !pChannels || !pRate)
		return BAD_VALUE;

	if (*pFormat != AUDIO_HW_IN_FORMAT) {
		LOGD("Invalid audio input format %d.", *pFormat);
		*pFormat = AUDIO_HW_IN_FORMAT;
		return BAD_VALUE;
	}

	uint32_t rate = getInputSampleRate(*pRate);

	if (rate != *pRate) {
		LOGD("Invalid audio input sample rate %d.", *pRate);
		*pRate = rate;
		return BAD_VALUE;
	}

	if (*pChannels != AudioSystem::CHANNEL_IN_MONO &&
	    *pChannels != AudioSystem::CHANNEL_IN_STEREO) {
		LOGD("Invalid audio input channels %d.", *pChannels);
		*pChannels = AUDIO_HW_IN_CHANNELS;
		return BAD_VALUE;
	}

	mHardware = hw;

	LOGD("AudioStreamInALSA::set(%d, %d, %u)",
						*pFormat, *pChannels, *pRate);

	mDevices = devices;
	mInputChannels = AUDIO_HW_IN_CHANNELS;
	mInputChannelCount = AudioSystem::popCount(mInputChannels);
	mChannels = *pChannels;
	mChannelCount = AudioSystem::popCount(mChannels);
	mBufferSize = getBufferSize(rate, mChannelCount);
	mSampleRate = rate;

	delete mChannelMixer;
	mChannelMixer = 0;

	delete mDownSampler;
	mDownSampler = 0;

	mInputProvider = this;

	if (mChannels != AUDIO_HW_IN_CHANNELS) {
		mChannelMixer = new ChannelMixer(mChannelCount,
				mInputChannelCount, AUDIO_HW_IN_PERIOD_SZ,
				mInputProvider);

		if (!mChannelMixer || mChannelMixer->initCheck() != NO_ERROR) {
			LOGE("AudioStreamInALSA::set() channel mixer "
								"init failed");
			return NO_INIT;
		}

		mInputProvider = mChannelMixer;
	}

	if (mSampleRate != AUDIO_HW_IN_SAMPLERATE) {
		mDownSampler = new DownSampler(mSampleRate,
					mChannelCount, AUDIO_HW_IN_PERIOD_SZ,
					mInputProvider);

		if (!mDownSampler || mDownSampler->initCheck() != NO_ERROR) {
			LOGE("AudioStreamInALSA::set() downsampler "
								"init failed");
			return NO_INIT;
		}

		mInputProvider = mDownSampler;
	}

	return NO_ERROR;
}

AudioStreamInALSA::~AudioStreamInALSA()
{
	TRACE();
	standby();

	if (mDownSampler)
		delete mDownSampler;

	if (mChannelMixer)
		delete mChannelMixer;

	if (mPcmIn)
		delete[] mPcmIn;
}

uint32_t AudioStreamInALSA::getInputSampleRate(uint32_t sampleRate)
{
	/* Sampling rates supported by input device */
	static const uint32_t inputSamplingRates[] = {
		8000, 11025, 16000, 22050, 44100
	};
	uint32_t i;
	uint32_t prevDelta = 0xffffffff;
	uint32_t delta;

	TRACE();

	for (i = 0; i < NELEM(inputSamplingRates); ++i) {
		delta = abs(sampleRate - inputSamplingRates[i]);

		if (delta > prevDelta)
			break;

		prevDelta = delta;
	}

	// i is always > 0 here
	return inputSamplingRates[i-1];
}

uint32_t AudioStreamInALSA::getInputRouteFromDevice(uint32_t device)
{
	TRACE();

	LOGD("getInputRouteFromDevice(%x)", device);

	switch (device) {
	case AudioSystem::DEVICE_IN_BUILTIN_MIC:
		return BIT(AudioRouter::ENDPOINT_MIC_MAIN);

	case AudioSystem::DEVICE_IN_BACK_MIC:
		return BIT(AudioRouter::ENDPOINT_MIC_SUB);

	case AudioSystem::DEVICE_IN_WIRED_HEADSET:
		return BIT(AudioRouter::ENDPOINT_MIC_HP);

	case AudioSystem::DEVICE_IN_BLUETOOTH_SCO_HEADSET:
		return BIT(AudioRouter::ENDPOINT_MIC_BT);

	case AudioSystem::DEVICE_IN_VOICE_CALL:
		return BIT(AudioRouter::ENDPOINT_PHONE_IN);

	default:
		return 0;
	}
}

int AudioStreamInALSA::wakeUp_l(void)
{
	TRACE_VERBOSE();

	if (!mStandby)
		return 0;

	AutoMutex hwLock(mHardware->lock());

	LOGD("AudioHardware pcm capture is exiting standby.");

	acquire_wake_lock(PARTIAL_WAKE_LOCK, "AudioInLock");

	sp<AudioStreamOutALSA> spOut = mHardware->getOutput();

	while (spOut != 0) {
		if (spOut->checkStandby()) {
			spOut.clear();
			continue;
		}

		int cnt = spOut->prepareLock();
		mHardware->lock().unlock();
		mLock.unlock();
		// Mutex acquisition order is always out -> in -> hw
		spOut->lock();
		mLock.lock();
		mHardware->lock().lock();

		// make sure that another thread did not change output state
		// while the mutex is released
		if (spOut == mHardware->getOutput()
		    && cnt == spOut->standbyCnt()) {
			LOGV("AudioStreamInALSA::read() force output standby");
			spOut->close_l();
			break;
		}

		spOut->unlock();
		spOut = mHardware->getOutput();
	}

	// spOut is not 0 here only if the output was active and has been
	// closed above

	// open output before input
	if (spOut != 0) {
		if (spOut->open_l() != NO_ERROR)
			spOut->doStandby_l();

		spOut->unlock();
	}

	open_l();

	if (!mPcm) {
		release_wake_lock("AudioInLock");
		return -1;
	}

	mStandby = false;

	return 0;
}

ssize_t AudioStreamInALSA::read(void *buffer, ssize_t bytes)
{
	TRACE_VERBOSE();
	status_t status = NO_INIT;
	int ret;
	size_t frames = bytes / frameSize();
	size_t framesIn = 0;
	Buffer buf;

	if (!mHardware) return NO_INIT;

	if (mSleepReq) {
		// 10ms are always shorter than the time to reconfigure
		// the audio path which is the only condition when mSleepReq
		// would be true.
		usleep(10000);
	}

	mLock.lock();

	if (wakeUp_l())
		goto error;

	buf.raw = buffer;
	mReadStatus = 0;

	do {
		buf.frameCount = frames - framesIn;
		ret = mInputProvider->getNextBuffer(&buf);
		buf.i16 += mChannelCount*buf.frameCount;
		framesIn += buf.frameCount;
	} while (framesIn < frames && !ret);

	bytes = framesIn*frameSize();

	if (!ret) {
		mLock.unlock();
		return bytes;
	}

	LOGE("read error: %d", ret);

	status = ret;

error:
	mLock.unlock();
	standby();

	// Simulate audio output timing in case of error
	usleep((((bytes * 1000) / frameSize()) * 1000) / sampleRate());

	return status;
}

status_t AudioStreamInALSA::standby()
{
	TRACE();

	if (!mHardware) {
		LOGW("Called standby() on input, but mHardware is NULL");
		return NO_INIT;
	}

	mSleepReq = true;
	mLock.lock();
	mSleepReq = false;
	mHardware->lock().lock();
	doStandby_l();
	mHardware->lock().unlock();
	mLock.unlock();

	return NO_ERROR;
}

void AudioStreamInALSA::doStandby_l()
{
	TRACE();
	++mStandbyCnt;

	if (!mStandby) {
		LOGD("AudioHardware pcm capture is going to standby.");
		release_wake_lock("AudioInLock");
		mStandby = true;
	}

	close_l();
}

void AudioStreamInALSA::close_l()
{
	TRACE();

	if (mPcm) {
		TRACE_DRIVER_IN(DRV_PCM_CLOSE)
		pcm_close(mPcm);
		TRACE_DRIVER_OUT
		mPcm = 0;
	}
}

status_t AudioStreamInALSA::open_l()
{
	TRACE();
	unsigned int flags;
	flags = PCM_IN
		| ((AUDIO_HW_IN_PERIOD_MULT - 1) << PCM_PERIOD_SZ_SHIFT)
		| ((AUDIO_HW_IN_PERIOD_CNT - PCM_PERIOD_CNT_MIN)
						<< PCM_PERIOD_CNT_SHIFT);

	LOGV("open pcm_in driver");

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

	if (mDownSampler) {
		mInPcmInBuf = 0;
		mDownSampler->reset();
	}

	uint32_t route = getInputRouteFromDevice(mDevices);
	LOGV("read() wakeup setting route %d", route);
	mHardware->setAudioRoute(AudioRouter::ROUTE_INPUT, route);

	return NO_ERROR;
}

status_t AudioStreamInALSA::dump(int fd, const Vector<String16> &args)
{
	TRACE();
	const size_t SIZE = 256;
	char buffer[SIZE];
	String8 result;

	bool locked = mLock.tryLock();

	if (!locked) {
		snprintf(buffer, SIZE,
			 "\n\t\tAudioStreamInALSA maybe deadlocked\n");
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
	write(fd, result.string(), result.size());

	return NO_ERROR;
}

bool AudioStreamInALSA::checkStandby()
{
	TRACE();
	return mStandby;
}

status_t AudioStreamInALSA::setParameters(const String8 &keyValuePairs)
{
	TRACE();
	AudioParameter param = AudioParameter(keyValuePairs);
	status_t status = NO_ERROR;
	int value;
	int ret;

	LOGD("AudioStreamInALSA::setParameters() %s", keyValuePairs.string());

	if (!mHardware)
		return NO_INIT;

	mSleepReq = true;
	mLock.lock();
	mSleepReq = false;

	ret = param.getInt(String8(AudioParameter::keyRouting), value);

	if (ret == NO_ERROR) {
		if (value != 0) {
			AutoMutex hwLock(mHardware->lock());

			if (mDevices != (uint32_t)value)
				doStandby_l();

			mDevices = (uint32_t)value;
		}

		param.remove(String8(AudioParameter::keyRouting));
	}

	mLock.unlock();

	if (param.size())
		status = BAD_VALUE;

	return status;
}

String8 AudioStreamInALSA::getParameters(const String8 &keys)
{
	TRACE();
	AudioParameter param = AudioParameter(keys);
	String8 value;
	String8 key = String8(AudioParameter::keyRouting);

	if (param.get(key, value) == NO_ERROR)
		param.addInt(key, (int)mDevices);

	LOGV("AudioStreamInALSA::getParameters() %s",
						param.toString().string());

	return param.toString();
}

status_t AudioStreamInALSA::getNextBuffer(BufferProvider::Buffer *buffer)
{
	TRACE_VERBOSE();

	if (!mPcm) {
		buffer->frameCount = 0;
		mReadStatus = NO_INIT;
		return NO_INIT;
	}

	if (!buffer->raw || !buffer->frameCount) {
		buffer->frameCount = 0;
		return BAD_VALUE;
	}

	TRACE_DRIVER_IN(DRV_PCM_READ)
	mReadStatus = pcm_read(mPcm, buffer->raw,
			buffer->frameCount*mInputChannelCount*sizeof(int16_t));
	TRACE_DRIVER_OUT

	if (mReadStatus) {
		buffer->frameCount = 0;
		return mReadStatus;
	}

	return 0;
}

size_t AudioStreamInALSA::getBufferSize(uint32_t sampleRate, int channelCount)
{
	TRACE();
	size_t ratio;

	switch (sampleRate) {
	case 8000:
	case 11025:
		ratio = 4;
		break;

	case 16000:
	case 22050:
		ratio = 2;
		break;

	case 44100:
	default:
		ratio = 1;
		break;
	}

	return (AUDIO_HW_IN_PERIOD_SZ*channelCount*sizeof(int16_t)) / ratio ;
}

int AudioStreamInALSA::prepareLock()
{
	TRACE();
	// request sleep next time read() is called so that caller can acquire
	// mLock
	mSleepReq = true;
	return mStandbyCnt;
}

void AudioStreamInALSA::lock()
{
	TRACE();
	mLock.lock();
	mSleepReq = false;
}

void AudioStreamInALSA::unlock()
{
	TRACE();
	mLock.unlock();
}

}; /* namespace android */
