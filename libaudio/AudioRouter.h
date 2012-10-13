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

#ifndef _AUDIOROUTER_H_
#define _AUDIOROUTER_H_

#include "config.h"
#include <stdint.h>
#include <sys/types.h>
#include <utils/RefBase.h>

extern "C" {
	struct mixer;
};

namespace android {

class AudioRouter : public RefBase {
public:
	enum PinType {
		TYPE_NONE = 0,
		TYPE_INT,
		TYPE_MUX
	};

	enum AudioEndpoint {
		ENDPOINT_MIC_MAIN = 1,
		ENDPOINT_MIC_SUB,
		ENDPOINT_MIC_HP,
		ENDPOINT_HP,
		ENDPOINT_PHONE_IN,
		ENDPOINT_PHONE_OUT,
		ENDPOINT_MIC_BT,
		ENDPOINT_BT,
		ENDPOINT_RCV,
		ENDPOINT_SPK,
		ENDPOINT_AMP,
		ENDPOINT_COUNT
	};

	enum RouteType {
		ROUTE_INPUT = 0,
		ROUTE_OUTPUT,
		ROUTE_VOICE_IN,
		ROUTE_VOICE_OUT,
		ROUTE_COUNT
	};

	struct AudioPinConfig {
		const char *ctl;
		PinType type;
		const char *strValue;
		const char *resetStrValue;
		int32_t intValue;
		int32_t resetIntValue;
	};
#define PIN_CONFIG_TERMINATOR \
 { NULL, AudioRouter::TYPE_NONE, NULL, NULL, 0, 0 }
#define PIN_CONFIG_MUX(ctl, active, inactive) \
 { ctl, AudioRouter::TYPE_MUX, active, inactive, 0, 0 }
#define PIN_CONFIG_INT(ctl, active, inactive) \
 { ctl, AudioRouter::TYPE_INT, NULL, NULL, active, inactive }
#define PIN_CONFIG_BOOL(ctl, active) \
		{ ctl, AudioRouter::TYPE_INT, NULL, NULL, active, !active }

	struct AudioRouteConfig {
		uint32_t route;
		const AudioPinConfig *config;
		void (*enable)(void);
		void (*disable)(void);
	};
#define ROUTE_CONFIG(route, config) \
		{ AudioRouter::route, config, NULL, NULL }
#define ROUTE_CONFIG_CALLBACKS(route, config, enable, disable) \
		{ AudioRouter::route, config, enable, disable }
#define ROUTE_CONFIG_TERMINATOR { 0, NULL, NULL, NULL }

	struct VolumeControl {
		int32_t endpoint;
		const char *control;
		unsigned int max;
	};
#define VOLUME_CONTROL(route, control, max) \
		{ AudioRouter::route, control, max }
#define VOLUME_CONTROL_TERMINATOR { 0, NULL, 0 }

private:
	void setEndpointVolume(const VolumeControl *volCtrl,
					uint32_t endpointMask, float volume);
	void muteOutputs(void);
	void updateVolume(void);
	void disableRoute(enum RouteType type);
	void enableRoute(enum RouteType type);
	void disablePinConfig(const AudioPinConfig *pin);
	void enablePinConfig(const AudioPinConfig *pin);

	static const AudioPinConfig initialPinConfig[];
	static const AudioRouteConfig *routeTables[ROUTE_COUNT];
	static const VolumeControl endpointVolCtrls[];
	static const VolumeControl pathVolCtrls[];

	uint32_t mRoute[ROUTE_COUNT];
	bool mDisabled[ROUTE_COUNT];

	float mPlaybackVolume;

	float mVoiceVol;
	float mMasterVol;

	struct mixer *mMixer;

	status_t mStatus;

public:
	static const AudioPinConfig inCallPinConfig[];

	AudioRouter();
	virtual ~AudioRouter();

	status_t initCheck()
	{
		return mStatus;
	}

	void setRouteDisable(enum RouteType type, bool disabled);
	void setAudioRoute(RouteType type, uint32_t route);

	void setVoiceVolume(float volume);
	void setMasterVolume(float volume);
};

}; /* namespace android */

#endif /* _AUDIOROUTER_H_ */
