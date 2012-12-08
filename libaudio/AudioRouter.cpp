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
#define LOG_TAG "AudioRouter"

#include <cutils/log.h>
#include "AudioRouter.h"
#include "utils.h"

extern "C" {
#include "alsa_audio.h"
};

namespace android {

/*
 * Bluetooth PCM helpers
 */

static int bluetoothInRefCnt = 0;
static struct pcm *bluetoothInPcm = NULL;

static void bluetoothInOpen(void)
{
	TRACE();
	LOGV("%s: ref count = %d", __func__, bluetoothInRefCnt);

	if (++bluetoothInRefCnt == 1) {
		bluetoothInPcm = pcm_open(PCM_BT | PCM_IN);

		if (pcm_ready(bluetoothInPcm))
			pcm_start(bluetoothInPcm);

		LOGV("%s: pcm error = %s", __func__, pcm_error(bluetoothInPcm));
	}
}

static void bluetoothInClose(void)
{
	TRACE();
	LOGV("%s: ref count = %d", __func__, bluetoothInRefCnt);

	if (--bluetoothInRefCnt == 0)
		pcm_close(bluetoothInPcm);
}

static int bluetoothOutRefCnt = 0;
static struct pcm *bluetoothOutPcm = NULL;

static void bluetoothOutOpen(void)
{
	TRACE();
	LOGV("%s: ref count = %d", __func__, bluetoothOutRefCnt);

	if (++bluetoothOutRefCnt == 1) {
		bluetoothOutPcm = pcm_open(PCM_BT | PCM_OUT);

		if (pcm_ready(bluetoothOutPcm))
			pcm_start(bluetoothOutPcm);

		LOGV("%s: pcm error = %s",
					__func__, pcm_error(bluetoothOutPcm));
	}
}

static void bluetoothOutClose(void)
{
	TRACE();
	LOGV("%s: ref count = %d", __func__, bluetoothOutRefCnt);

	if (--bluetoothOutRefCnt == 0)
		pcm_close(bluetoothOutPcm);
}


/*
 * Pin configurations
 */

const AudioRouter::AudioPinConfig AudioRouter::initialPinConfig[] = {
	PIN_CONFIG_BOOL("Line Output 1 Differential", 1),
	PIN_CONFIG_BOOL("Line Output 3 Differential", 1),
	PIN_CONFIG_INT("DATT-B DATT-B", 0, 0), // Software Fixi
	PIN_CONFIG_INT("DATT-B DATT-B", 231, 0),
	PIN_CONFIG_MUX("BVMX Mux", "PCM-A", 0),
	PIN_CONFIG_MUX("SDOA Mux", "SRC-A", 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig inputMicMainPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 1 Differential", 1),
	PIN_CONFIG_BOOL("Main Mic Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN2", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 13, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig inputMicSubPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 2 Differential", 1),
	PIN_CONFIG_BOOL("Sub Mic Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN2", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 9, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig inputHeadsetPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 3 Differential", 1),
	PIN_CONFIG_BOOL("Jack Mic Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN2", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN3", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 9, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig inputPhonePinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 4 Differential", 1),
	PIN_CONFIG_BOOL("Main Mic Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN4", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 13, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig inputBtPinConfigs[] = {
	PIN_CONFIG_MUX("SDOL Mux", "SRC-B", "ADC Left"),
	PIN_CONFIG_MUX("SDOR Mux", "SRC-B", "ADC Right"),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 13, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioRouteConfig inputRouteConfigs[] = {
	ROUTE_CONFIG(ENDPOINT_MIC_MAIN, inputMicMainPinConfigs),
	ROUTE_CONFIG(ENDPOINT_MIC_SUB, inputMicSubPinConfigs),
	ROUTE_CONFIG(ENDPOINT_MIC_HP, inputHeadsetPinConfigs),
	ROUTE_CONFIG(ENDPOINT_PHONE_IN, inputPhonePinConfigs),
	ROUTE_CONFIG_CALLBACKS(ENDPOINT_MIC_BT, inputBtPinConfigs,
	bluetoothInOpen, bluetoothInClose),
	ROUTE_CONFIG_TERMINATOR
};

static const AudioRouter::AudioPinConfig outputRcvPinConfigs[] = {
	PIN_CONFIG_BOOL("LOUT1 Mixer DACL", 1),
	PIN_CONFIG_BOOL("ROUT1 Mixer DACR", 1),
	PIN_CONFIG_BOOL("Earpiece Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig outputSpkPinConfigs[] = {
	PIN_CONFIG_BOOL("Speaker Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig outputHpPinConfigs[] = {
	PIN_CONFIG_BOOL("Headphones Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig outputAmpPinConfigs[] = {
	PIN_CONFIG_BOOL("LOUT2 Mixer DACHL", 1),
	PIN_CONFIG_BOOL("ROUT2 Mixer DACHR", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig outputBtPinConfigs[] = {
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioRouteConfig outputRouteConfigs[] = {
	ROUTE_CONFIG(ENDPOINT_RCV, outputRcvPinConfigs),
	ROUTE_CONFIG(ENDPOINT_AMP, outputAmpPinConfigs),
	ROUTE_CONFIG(ENDPOINT_SPK, outputSpkPinConfigs),
	ROUTE_CONFIG(ENDPOINT_HP, outputHpPinConfigs),
	ROUTE_CONFIG_CALLBACKS(ENDPOINT_BT, outputBtPinConfigs,
	bluetoothOutOpen, bluetoothOutClose),
	ROUTE_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceInMicMainPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 1 Differential", 1),
	PIN_CONFIG_BOOL("Main Mic Switch", 1),
	PIN_CONFIG_BOOL("LOUT3 Mixer LINS1", 1),
	PIN_CONFIG_BOOL("ROUT3 Mixer RINS1", 1),
	PIN_CONFIG_BOOL("GSM Send Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN4", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 13, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceInMicSubPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 2 Differential", 1),
	PIN_CONFIG_BOOL("Sub Mic Switch", 1),
	PIN_CONFIG_BOOL("LOUT3 Mixer LINS2", 1),
	PIN_CONFIG_BOOL("ROUT3 Mixer RINS2", 1),
	PIN_CONFIG_BOOL("GSM Send Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN4", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 13, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceInHeadsetPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 3 Differential", 1),
	PIN_CONFIG_BOOL("Jack Mic Switch", 1),
	PIN_CONFIG_BOOL("LOUT3 Mixer LINS3", 1),
	PIN_CONFIG_BOOL("ROUT3 Mixer RINS3", 1),
	PIN_CONFIG_BOOL("GSM Send Switch", 1),
	PIN_CONFIG_MUX("RIN MUX", "RIN4", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN3", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 9, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceInBtPinConfigs[] = {
	PIN_CONFIG_MUX("SRA Mux", "SRMXR Mux", "MIXD"),
	PIN_CONFIG_MUX("SRMXL Mux", "SRC-B", "PFMXL Mux"),
	PIN_CONFIG_BOOL("LOUT3 Mixer DACSL", 1),
	PIN_CONFIG_BOOL("GSM Send Switch", 1),
	PIN_CONFIG_MUX("SDOL Mux", "SRC-B", "ADC Left"),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 5, -1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioRouteConfig voiceInRouteConfigs[] = {
	ROUTE_CONFIG(ENDPOINT_MIC_MAIN, voiceInMicMainPinConfigs),
	ROUTE_CONFIG(ENDPOINT_MIC_SUB, voiceInMicSubPinConfigs),
	ROUTE_CONFIG(ENDPOINT_MIC_HP, voiceInHeadsetPinConfigs),
	ROUTE_CONFIG_CALLBACKS(ENDPOINT_MIC_BT, voiceInBtPinConfigs,
	bluetoothInOpen, bluetoothInClose),
	ROUTE_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceOutRcvPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 4 Differential", 1),
	PIN_CONFIG_BOOL("LOUT1 Mixer DACL", 1),
	PIN_CONFIG_BOOL("ROUT1 Mixer DACR", 1),
	PIN_CONFIG_BOOL("LOUT1 Mixer LINL4", 1),
	PIN_CONFIG_BOOL("ROUT1 Mixer RINR4", 1),
	PIN_CONFIG_BOOL("GSM Receive Switch", 1),
	PIN_CONFIG_BOOL("Earpiece Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceOutAmpPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 4 Differential", 1),
	PIN_CONFIG_BOOL("LOUT2 Mixer DACHL", 1),
	PIN_CONFIG_BOOL("ROUT2 Mixer DACHR", 1),
	PIN_CONFIG_BOOL("LOUT2 Mixer LINH4", 1),
	PIN_CONFIG_BOOL("ROUT2 Mixer RINH4", 1),
	PIN_CONFIG_BOOL("GSM Receive Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceOutSpkPinConfigs[] = {
	PIN_CONFIG_BOOL("Speaker Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceOutHpPinConfigs[] = {
	PIN_CONFIG_BOOL("Headphones Switch", 1),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioPinConfig voiceOutBtPinConfigs[] = {
	PIN_CONFIG_BOOL("Line Input 4 Differential", 1),
	PIN_CONFIG_BOOL("GSM Receive Switch", 1),
	PIN_CONFIG_MUX("PFMXR Mux", "PFMXR Mixer", "SDTI Right"),
	PIN_CONFIG_MUX("RIN MUX", "RIN4", 0),
	PIN_CONFIG_MUX("LIN MUX", "LIN1", 0),
	PIN_CONFIG_INT("Mic Amp Capture Volume", 5, 0),
	PIN_CONFIG_TERMINATOR,
};

static const AudioRouter::AudioRouteConfig voiceOutRouteConfigs[] = {
	ROUTE_CONFIG(ENDPOINT_RCV, voiceOutRcvPinConfigs),
	ROUTE_CONFIG(ENDPOINT_AMP, voiceOutAmpPinConfigs),
	ROUTE_CONFIG(ENDPOINT_SPK, voiceOutSpkPinConfigs),
	ROUTE_CONFIG(ENDPOINT_HP, voiceOutHpPinConfigs),
	ROUTE_CONFIG_CALLBACKS(ENDPOINT_BT, voiceOutBtPinConfigs,
	bluetoothOutOpen, bluetoothOutClose),
	ROUTE_CONFIG_TERMINATOR,
};

const AudioRouter::AudioRouteConfig *AudioRouter::routeTables[] = {
	inputRouteConfigs,
	outputRouteConfigs,
	voiceInRouteConfigs,
	voiceOutRouteConfigs
};

const AudioRouter::VolumeControl AudioRouter::endpointVolCtrls[] = {
	VOLUME_CONTROL(ENDPOINT_HP, "MAX9877 Amp HP Playback Volume", 28),
	VOLUME_CONTROL(ENDPOINT_PHONE_OUT, "Line Output3 Playback Volume", 3),
	VOLUME_CONTROL(ENDPOINT_RCV, "Line Output1 Playback Volume", 6),
	VOLUME_CONTROL(ENDPOINT_SPK, "MAX9877 Amp Speaker Playback Volume", 28),
	VOLUME_CONTROL_TERMINATOR,
};

const AudioRouter::VolumeControl AudioRouter::pathVolCtrls[] = {
	VOLUME_CONTROL(ROUTE_INPUT, "Mic Amp Capture Volume", 15),
	VOLUME_CONTROL(ROUTE_OUTPUT, "Master Playback Volume", 231),
	VOLUME_CONTROL_TERMINATOR,
};

AudioRouter::AudioRouter() :
	mPlaybackVolume(1.0f),
	mVoiceVol(0.0f),
	mMasterVol(0.0f),
	mStatus(NO_INIT)
{
	TRACE();

	TRACE_DRIVER_IN(DRV_MIXER_OPEN)
	mMixer = mixer_open();
	TRACE_DRIVER_OUT

	if (!mMixer) {
		LOGE("%s: Failed to open mixer", __func__);
		return;
	}

	enablePinConfig(initialPinConfig);

	for (int i = 0; i < ROUTE_COUNT; ++i) {
		mRoute[i] = 0;
		mDisabled[i] = false;
	}

	mStatus = NO_ERROR;
}

AudioRouter::~AudioRouter()
{
	TRACE();

	if (mMixer)
		mixer_close(mMixer);

	mStatus = NO_INIT;
}

void AudioRouter::disablePinConfig(const AudioPinConfig *pin)
{
	TRACE();
	struct mixer_ctl *ctl;
	const AudioPinConfig *p = pin;

	while (p->type)
		++p;

	if (p == pin)
		return;

	--p;

	/* Disable requested route */
	do {
		TRACE_DRIVER_IN(DRV_MIXER_GET)
		ctl = mixer_get_control(mMixer, p->ctl, 0);
		TRACE_DRIVER_OUT

		if (!ctl) {
			LOGE("failed to get control '%s'", p->ctl);
			continue;
		}

		if (p->type == TYPE_MUX) {
			if (!p->resetStrValue)
				continue;

			TRACE_DRIVER_IN(DRV_MIXER_SEL)

			if (mixer_ctl_select(ctl, p->resetStrValue))
				LOGE("failed to set control '%s' to '%s'",
						p->ctl, p->resetStrValue);

			TRACE_DRIVER_OUT
			continue;
		}

		if (p->resetIntValue < 0)
			continue;

		TRACE_DRIVER_IN(DRV_MIXER_SEL)

		if (mixer_ctl_set(ctl, CTL_VALUE_RAW | p->resetIntValue))
			LOGE("failed to set control '%s' to %d",
						p->ctl, p->resetIntValue);

		TRACE_DRIVER_OUT
	} while (p-- != pin);
}

void AudioRouter::enablePinConfig(const AudioPinConfig *pin)
{
	TRACE();
	struct mixer_ctl *ctl;

	/* Configure requested route */
	for (; pin->type; ++pin) {
		TRACE_DRIVER_IN(DRV_MIXER_GET)
		ctl = mixer_get_control(mMixer, pin->ctl, 0);
		TRACE_DRIVER_OUT

		if (!ctl) {
			LOGE("failed to get control '%s'", pin->ctl);
			continue;
		}

		if (pin->type == TYPE_MUX) {
			TRACE_DRIVER_IN(DRV_MIXER_SEL)

			if (mixer_ctl_select(ctl, pin->strValue))
				LOGE("failed to set control '%s' to '%s'",
						pin->ctl, pin->strValue);

			TRACE_DRIVER_OUT
			continue;
		}

		TRACE_DRIVER_IN(DRV_MIXER_SEL)

		if (mixer_ctl_set(ctl, CTL_VALUE_RAW | pin->intValue))
			LOGE("failed to set control '%s' to %d",
						pin->ctl, pin->intValue);

		TRACE_DRIVER_OUT
	}
}

void AudioRouter::disableRoute(enum RouteType type)
{
	TRACE();
	const AudioRouteConfig *route;

	for (route = routeTables[type]; route->route; ++route) {
		if (!(mRoute[type] & BIT(route->route)))
			continue;

		disablePinConfig(route->config);

		if (route->disable)
			route->disable();
	}
}

void AudioRouter::enableRoute(enum RouteType type)
{
	TRACE();
	const AudioRouteConfig *route;

	for (route = routeTables[type]; route->route; ++route) {
		if (!(mRoute[type] & BIT(route->route)))
			continue;

		if (route->enable)
			route->enable();

		enablePinConfig(route->config);
	}
}

void AudioRouter::setRouteDisable(enum RouteType type, bool disabled)
{
	TRACE();

	if (mDisabled[type] == disabled)
		return;

	if (disabled) {
		disableRoute(type);
	} else {
		if (type == ROUTE_OUTPUT || type == ROUTE_VOICE_OUT)
			muteOutputs();

		enableRoute(type);

		if (type == ROUTE_OUTPUT || type == ROUTE_VOICE_OUT)
			updateVolume();
	}

	mDisabled[type] = disabled;
}

void AudioRouter::setAudioRoute(enum RouteType type, uint32_t route)
{
	TRACE();
	const AudioRouteConfig *cfg;

	if (mDisabled[type]) {
		mRoute[type] = route;
		return;
	}

	if (type == ROUTE_OUTPUT || type == ROUTE_VOICE_OUT)
		muteOutputs();

	disableRoute(type);
	mRoute[type] = route;
	enableRoute(type);

	if (type == ROUTE_OUTPUT || type == ROUTE_VOICE_OUT)
		updateVolume();
}

void AudioRouter::setEndpointVolume(const VolumeControl *volCtrl,
					uint32_t endpointMask, float volume)
{
	TRACE();
	struct mixer_ctl *ctl;

	for (; volCtrl->endpoint; ++volCtrl) {
		if (!(endpointMask & BIT(volCtrl->endpoint)))
			continue;

		ctl = mixer_get_control(mMixer, volCtrl->control, 0);

		if (ctl)
			mixer_ctl_set(ctl, CTL_VALUE_RAW |
					(uint32_t)(volume * volCtrl->max));
		else
			LOGE("failed to get control '%s'", volCtrl->control);
	}
}

void AudioRouter::muteOutputs(void)
{
	TRACE();

	setEndpointVolume(endpointVolCtrls, mRoute[ROUTE_OUTPUT], 0.0f);
	setEndpointVolume(endpointVolCtrls, mRoute[ROUTE_VOICE_OUT], 0.0f);
}

void AudioRouter::updateVolume(void)
{
	TRACE();
	float playbackVolume = 1.0f;
	float playbackOutputVolume = mMasterVol;
	float voiceOutputVolume = mVoiceVol;

	/* Calculate volume factors */
	if (mRoute[ROUTE_OUTPUT] & mRoute[ROUTE_VOICE_OUT]) {
		if (mVoiceVol > mMasterVol) {
			playbackOutputVolume = mVoiceVol;
			voiceOutputVolume = mVoiceVol;
			playbackVolume = mMasterVol / mVoiceVol;
		} else {
			playbackOutputVolume = mVoiceVol;
			voiceOutputVolume = mVoiceVol;
			playbackVolume = 1.0f;
		}
	}

	if (playbackVolume <= mPlaybackVolume)
		/* Adjust playback volume */
		setEndpointVolume(pathVolCtrls,
					BIT(ROUTE_OUTPUT), playbackVolume);

	/* Adjust playback output volume */
	setEndpointVolume(endpointVolCtrls,
				mRoute[ROUTE_OUTPUT], playbackOutputVolume);

	/* Adjust voice output volume */
	setEndpointVolume(endpointVolCtrls,
				mRoute[ROUTE_VOICE_OUT], voiceOutputVolume);

	if (playbackVolume > mPlaybackVolume)
		/* Adjust playback volume */
		setEndpointVolume(pathVolCtrls,
					BIT(ROUTE_OUTPUT), playbackVolume);

	/* Remember new endpoint volume levels */
	mPlaybackVolume = playbackVolume;
}

void AudioRouter::setVoiceVolume(float volume)
{
	TRACE();

	if (!mMixer) {
		LOGW("setOutputVolume called, but mMixer is NULL");
		return;
	}

	mVoiceVol = volume;

	updateVolume();
}

void AudioRouter::setMasterVolume(float volume)
{
	TRACE();

	if (!mMixer) {
		LOGW("setOutputVolume called, but mMixer is NULL");
		return;
	}

	mMasterVol = volume;

	updateVolume();
}

}; /* namespace android */
