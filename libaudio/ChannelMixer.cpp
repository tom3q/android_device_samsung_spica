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
#define LOG_TAG "ChannelMixer"

#include <utils/Errors.h>
#include <cutils/log.h>
#include "ChannelMixer.h"
#include "utils.h"

namespace android {

/*
 * Channel mixer
 */

ChannelMixer::ChannelMixer(uint32_t outChannelCount, uint32_t channelCount,
				uint32_t frameCount, BufferProvider *provider) :
	mStatus(NO_INIT),
	mBuffer(0),
	mProvider(provider),
	mOutChannelCount(outChannelCount),
	mChannelCount(channelCount)
{
	TRACE();
	LOGV("ChannelMixer() cstor %p channels %d frames %d",
					this, mChannelCount, frameCount);

	if (outChannelCount != 1 || channelCount != 2) {
		LOGE("ChannelMixer cstor: bad conversion: %d => %d",
					mChannelCount, outChannelCount);
		return;
	}

	mBuffer = new int16_t[frameCount*channelCount];

	if (!mBuffer) {
		LOGE("ChannelMixer: Failed to allocate work buffer");
		return;
	}

	mStatus = NO_ERROR;
}

ChannelMixer::~ChannelMixer()
{
	if (mBuffer)
		delete[] mBuffer;
}

status_t ChannelMixer::getNextBuffer(
	BufferProvider::Buffer *buffer)
{
	TRACE_VERBOSE();
	status_t ret;
	BufferProvider::Buffer buf;

	if (!mProvider)
		return NO_INIT;

	buf.i16 = mBuffer;
	buf.frameCount = buffer->frameCount;

	ret = mProvider->getNextBuffer(&buf);

	if (ret != 0) {
		LOGE("%s: mProvider->getNextBuffer() failed (%d)",
								__func__, ret);
		return ret;
	}

	short *in = buf.i16;
	short *out = buffer->i16;

	for (unsigned int i = 0; i < buf.frameCount; ++i, ++out, in += 2)
		out[0] = (in[0] + in[1]) / 2;

	buffer->frameCount = buf.frameCount;

	return NO_ERROR;
}

}; /* namespace android */
