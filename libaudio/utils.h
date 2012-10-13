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

#ifndef _ALSA_SOC_AUDIO_UTILS_H_
#define _ALSA_SOC_AUDIO_UTILS_H_

namespace android {

//#define DRIVER_TRACE
#define DEBUG_TRACE
//#define DEBUG_TRACE_VERBOSE

class Tracer {
	const char *name;
	static int level;
public:
	Tracer(const char *name) :
		name(name) {
		++level;
		LOG(LOG_VERBOSE, "Tracer", "%*s %s enter", 2*level, ">", name);
	}

	~Tracer() {
		LOG(LOG_VERBOSE, "Tracer", "%*s %s leave", 2*level, "<", name);
		--level;
	}
};

#ifdef DEBUG_TRACE

#define TRACE()		Tracer __tracer__LINE__(__func__)

#ifdef DEBUG_TRACE_VERBOSE
#define TRACE_VERBOSE()	Tracer __tracer__LINE__(__func__)
#else
#define TRACE_VERBOSE()
#endif

#else

#define TRACE()
#define TRACE_VERBOSE()

#endif

#define NELEM(x)	(sizeof((x)) / sizeof((x)[0]))
#define BIT(x)		(1UL << (x))

enum {
	DRV_NONE,
	DRV_PCM_OPEN,
	DRV_PCM_CLOSE,
	DRV_PCM_WRITE,
	DRV_PCM_READ,
	DRV_MIXER_OPEN,
	DRV_MIXER_CLOSE,
	DRV_MIXER_GET,
	DRV_MIXER_SEL
};

#ifdef DRIVER_TRACE
#define TRACE_DRIVER_IN(op) mDriverOp = op;
#define TRACE_DRIVER_OUT mDriverOp = DRV_NONE;
#else
#define TRACE_DRIVER_IN(op)
#define TRACE_DRIVER_OUT
#endif

}; /* namespace android */

#endif /* _ALSA_SOC_AUDIO_UTILS_H_ */
