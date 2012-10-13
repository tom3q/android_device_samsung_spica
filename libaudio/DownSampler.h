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

#ifndef _DOWNSAMPLER_H_
#define _DOWNSAMPLER_H_

#include "BufferProvider.h"

namespace android {

class DownSampler : public BufferProvider {
public:
	DownSampler(uint32_t outSampleRate, uint32_t channelCount,
				uint32_t frameCount, BufferProvider *provider);
	virtual ~DownSampler();

	status_t initCheck()
	{
		return mStatus;
	}

	void reset();

	virtual status_t getNextBuffer(Buffer *buffer);

private:
	status_t mStatus;
	BufferProvider *mProvider;
	uint32_t mSampleRate;
	uint32_t mChannelCount;
	uint32_t mFrameCount;
	int mOutBufIdx;
	int16_t *mTmpBuf[4];
	int mInTmpBuf[4];
};

}; /* namespace android */

#endif /* _DOWNSAMPLER_H_ */
