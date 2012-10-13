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

#ifndef _CHANNELMIXER_H_
#define _CHANNELMIXER_H_

#include "BufferProvider.h"

namespace android {

class ChannelMixer : public BufferProvider {
public:
	ChannelMixer(uint32_t outChannelCount, uint32_t channelCount,
			uint32_t frameCount, BufferProvider *provider);
	virtual ~ChannelMixer();

	status_t initCheck()
	{
		return mStatus;
	}

	virtual status_t getNextBuffer(Buffer *buffer);

private:
	status_t mStatus;
	int16_t *mBuffer;
	BufferProvider *mProvider;
	uint32_t mOutChannelCount;
	uint32_t mChannelCount;
};

}; /* namespace android */

#endif /* _CHANNELMIXER_H_ */
