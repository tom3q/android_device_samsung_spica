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

#ifndef _ALSA_SOC_AUDIO_CONFIG_H
#define _ALSA_SOC_AUDIO_CONFIG_H

// TODO: determine actual audio DSP and hardware latency
// Additionnal latency introduced by audio DSP and hardware in ms
#define AUDIO_HW_OUT_LATENCY_MS 0
// Default audio output sample rate
#define AUDIO_HW_OUT_SAMPLERATE 44100
// Default audio output channel mask
#define AUDIO_HW_OUT_CHANNELS (AudioSystem::CHANNEL_OUT_STEREO)
// Default audio output sample format
#define AUDIO_HW_OUT_FORMAT (AudioSystem::PCM_16_BIT)
// Kernel pcm out buffer size in frames at 44.1kHz
#define AUDIO_HW_OUT_PERIOD_MULT 8 // (8 * 128 = 1024 frames)
#define AUDIO_HW_OUT_PERIOD_SZ (PCM_PERIOD_SZ_MIN * AUDIO_HW_OUT_PERIOD_MULT)
#define AUDIO_HW_OUT_PERIOD_CNT 4
// Default audio output buffer size in bytes
#define AUDIO_HW_OUT_PERIOD_BYTES (AUDIO_HW_OUT_PERIOD_SZ * 2 * sizeof(int16_t))

// Default audio input sample rate
#define AUDIO_HW_IN_SAMPLERATE 44100
// Default audio input channel mask
#define AUDIO_HW_IN_CHANNELS (AudioSystem::CHANNEL_IN_STEREO)
// Default audio input sample format
#define AUDIO_HW_IN_FORMAT (AudioSystem::PCM_16_BIT)
// Number of buffers in audio driver for input
//#define AUDIO_HW_NUM_IN_BUF 2
// Kernel pcm in buffer size in frames at 44.1kHz (before resampling)
#define AUDIO_HW_IN_PERIOD_MULT 8 // (8 * 128 = 1024 frames)
#define AUDIO_HW_IN_PERIOD_SZ (PCM_PERIOD_SZ_MIN * AUDIO_HW_IN_PERIOD_MULT)
#define AUDIO_HW_IN_PERIOD_CNT 4
// Default audio input buffer size in bytes
#define AUDIO_HW_IN_PERIOD_BYTES (AUDIO_HW_IN_PERIOD_SZ * 2 * sizeof(int16_t))

#endif /* _ALSA_SOC_AUDIO_CONFIG_H */
