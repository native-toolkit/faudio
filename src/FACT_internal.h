/* FAudio - XAudio Reimplementation for FNA
 *
 * Copyright (c) 2011-2018 Ethan Lee, Luigi Auriemma, and the MonoGame Team
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

#include "FACT.h"
#include "FACT3D.h"
#include "FAudio_internal.h"

/* Internal Constants */

#define FACT_VOLUME_0 180

/* Internal AudioEngine Types */

typedef struct FACTAudioEngineCallback
{
	FAudioEngineCallback callback;
	FACTAudioEngine *engine;
} FACTAudioEngineCallback;

typedef struct FACTAudioCategory
{
	uint8_t instanceLimit;
	uint16_t fadeInMS;
	uint16_t fadeOutMS;
	uint8_t maxInstanceBehavior;
	int16_t parentCategory;
	float volume;
	uint8_t visibility;
	uint8_t instanceCount;
	float currentVolume;
} FACTAudioCategory;

typedef struct FACTVariable
{
	uint8_t accessibility;
	float initialValue;
	float minValue;
	float maxValue;
} FACTVariable;

typedef struct FACTRPCPoint
{
	float x;
	float y;
	uint8_t type;
} FACTRPCPoint;

typedef enum FACTRPCParameter
{
	RPC_PARAMETER_VOLUME,
	RPC_PARAMETER_PITCH,
	RPC_PARAMETER_REVERBSEND,
	RPC_PARAMETER_FILTERFREQUENCY,
	RPC_PARAMETER_FILTERQFACTOR,
	RPC_PARAMETER_COUNT /* If >=, DSP Parameter! */
} FACTRPCParameter;

typedef struct FACTRPC
{
	uint16_t variable;
	uint8_t pointCount;
	uint16_t parameter;
	FACTRPCPoint *points;
} FACTRPC;

typedef struct FACTDSPParameter
{
	uint8_t type;
	float value;
	float minVal;
	float maxVal;
	uint16_t unknown;
} FACTDSPParameter;

typedef struct FACTDSPPreset
{
	uint8_t accessibility;
	uint32_t parameterCount;
	FACTDSPParameter *parameters;
} FACTDSPPreset;

/* Internal SoundBank Types */

typedef struct FACTCueData
{
	uint8_t flags;
	uint32_t sbCode;
	uint32_t transitionOffset;
	uint8_t instanceLimit;
	uint16_t fadeInMS;
	uint16_t fadeOutMS;
	uint8_t maxInstanceBehavior;
	uint8_t instanceCount;
} FACTCueData;

typedef enum
{
	FACTEVENT_STOP =				0,
	FACTEVENT_PLAYWAVE =				1,
	FACTEVENT_PLAYWAVETRACKVARIATION =		3,
	FACTEVENT_PLAYWAVEEFFECTVARIATION =		4,
	FACTEVENT_PLAYWAVETRACKEFFECTVARIATION =	6,
	FACTEVENT_PITCH =				7,
	FACTEVENT_VOLUME =				8,
	FACTEVENT_MARKER =				9,
	FACTEVENT_PITCHREPEATING =			16,
	FACTEVENT_VOLUMEREPEATING =			17,
	FACTEVENT_MARKERREPEATING =			18
} FACTEventType;

typedef struct FACTSimpleWave
{
	uint16_t track;
	uint8_t wavebank;
} FACTSimpleWave;

typedef struct FACTEvent_PlayWave
{
	uint8_t flags;
	uint8_t loopCount;
	uint16_t position;
	uint16_t angle;

	/* Track Variation */
	uint8_t isComplex;
	union
	{
		FACTSimpleWave simple;
		struct
		{
			uint16_t variation;
			uint16_t trackCount;
			uint16_t *tracks;
			uint8_t *wavebanks;
			uint8_t *weights;
		} complex;
	};

	/* Effect Variation */
	int16_t minPitch;
	int16_t maxPitch;
	float minVolume;
	float maxVolume;
	float minFrequency;
	float maxFrequency;
	float minQFactor;
	float maxQFactor;
	uint16_t variationFlags;
} FACTEvent_PlayWave;

typedef struct FACTEvent_SetValue
{
	uint8_t settings;
	uint16_t repeats;
	uint16_t frequency;
	union
	{
		struct
		{
			float initialValue;
			float initialSlope;
			float slopeDelta;
			uint16_t duration;
		} ramp;
		struct
		{
			uint8_t flags;
			float value1;
			float value2;
		} equation;
	};
} FACTEvent_SetValue;

typedef struct FACTEvent_Stop
{
	uint8_t flags;
} FACTEvent_Stop;

typedef struct FACTEvent_Marker
{
	uint32_t marker;
	uint16_t repeats;
	uint16_t frequency;
} FACTEvent_Marker;

typedef struct FACTEvent
{
	uint16_t type;
	uint16_t timestamp;
	uint16_t randomOffset;
	union
	{
		FACTEvent_PlayWave wave;
		FACTEvent_SetValue value;
		FACTEvent_Stop stop;
		FACTEvent_Marker marker;
	};
} FACTEvent;

typedef struct FACTTrack
{
	uint32_t code;

	float volume;
	uint8_t filter;
	uint8_t qfactor;
	uint16_t frequency;

	uint8_t rpcCodeCount;
	uint32_t *rpcCodes;

	uint8_t eventCount;
	FACTEvent *events;
} FACTTrack;

typedef struct FACTSound
{
	uint8_t flags;
	uint16_t category;
	float volume;
	int16_t pitch;
	uint8_t priority;

	uint8_t trackCount;
	uint8_t rpcCodeCount;
	uint8_t dspCodeCount;

	FACTTrack *tracks;
	uint32_t *rpcCodes;
	uint32_t *dspCodes;
} FACTSound;

typedef struct FACTInstanceRPCData
{
	float rpcVolume;
	float rpcPitch;
	float rpcFilterFreq;
} FACTInstanceRPCData;

typedef struct FACTEventInstance
{
	uint32_t timestamp;
	uint16_t loopCount;
	uint8_t finished;
	union
	{
		float value;
		uint32_t valuei;
	};
} FACTEventInstance;

typedef struct FACTTrackInstance
{
	/* Tracks which events have fired */
	FACTEventInstance *events;

	/* RPC instance data */
	FACTInstanceRPCData rpcData;

	/* Wave playback */
	FACTWave *wave;
	FACTWave *upcomingWave;
	FACTEvent *waveEvt;
	FACTEventInstance *waveEvtInst;
	float baseVolume;
	int16_t basePitch;
	float baseQFactor;
	float baseFrequency;
} FACTTrackInstance;

typedef struct FACTSoundInstance
{
	/* Base Sound reference */
	FACTSound *sound;

	/* Per-instance track information */
	FACTTrackInstance *tracks;

	/* RPC instance data */
	FACTInstanceRPCData rpcData;
} FACTSoundInstance;

typedef struct FACTVariation
{
	union
	{
		FACTSimpleWave simple;
		uint32_t soundCode;
	};
	float minWeight;
	float maxWeight;
	uint32_t linger;
} FACTVariation;

typedef struct FACTVariationTable
{
	uint8_t flags;
	int16_t variable;
	uint8_t isComplex;

	uint16_t entryCount;
	FACTVariation *entries;
} FACTVariationTable;

/* Internal Wave Types */

typedef struct FACTWaveCallback
{
	FAudioVoiceCallback callback;
	FACTWave *wave;
} FACTWaveCallback;

/* Public XACT Types */

struct FACTAudioEngine
{
	uint16_t categoryCount;
	uint16_t variableCount;
	uint16_t rpcCount;
	uint16_t dspPresetCount;
	uint16_t dspParameterCount;

	char **categoryNames;
	char **variableNames;
	uint32_t *rpcCodes;
	uint32_t *dspPresetCodes;

	FACTAudioCategory *categories;
	FACTVariable *variables;
	FACTRPC *rpcs;
	FACTDSPPreset *dspPresets;

	/* Engine references */
	FACTSoundBank *sbList;
	FACTWaveBank *wbList;
	float *globalVariableValues;

	/* FAudio references */
	FAudio *audio;
	FAudioMasteringVoice *master;
	FACTAudioEngineCallback callback;
};

struct FACTSoundBank
{
	/* Engine references */
	FACTAudioEngine *parentEngine;
	FACTSoundBank *next;
	FACTCue *cueList;

	/* Array sizes */
	uint16_t cueCount;
	uint8_t wavebankCount;
	uint16_t soundCount;
	uint16_t variationCount;

	/* Strings, strings everywhere! */
	char **wavebankNames;
	char **cueNames;

	/* Actual SoundBank information */
	char *name;
	FACTCueData *cues;
	FACTSound *sounds;
	uint32_t *soundCodes;
	FACTVariationTable *variations;
	uint32_t *variationCodes;
};

struct FACTWaveBank
{
	/* Engine references */
	FACTAudioEngine *parentEngine;
	FACTWave *waveList;
	FACTWaveBank *next;

	/* Actual WaveBank information */
	char *name;
	uint32_t entryCount;
	FACTWaveBankEntry *entries;
	uint32_t *entryRefs;

	/* I/O information */
	uint16_t streaming;
	FAudioIOStream *io;
};

struct FACTWave
{
	/* Engine references */
	FACTWaveBank *parentBank;
	FACTWave *next;
	uint16_t index;

	/* Playback */
	uint32_t state;
	float volume;
	int16_t pitch;
	uint32_t initialPosition;
	uint8_t loopCount;

	/* Stream data */
	uint32_t streamSize;
	uint32_t streamOffset;
	uint8_t *streamCache;

	/* FAudio references */
	FAudioSourceVoice *voice;
	FACTWaveCallback callback;
};

struct FACTCue
{
	/* Engine references */
	FACTSoundBank *parentBank;
	FACTCue *next;
	uint8_t managed;
	uint16_t index;

	/* Sound data */
	FACTCueData *data;
	union
	{
		FACTVariationTable *variation;

		/* This is only used in scenarios where there is only one
		 * Sound; XACT does not generate variation tables for
		 * Cues with only one Sound.
		 */
		FACTSound *sound;
	};

	/* Instance data */
	float *variableValues;
	float interactive;

	/* Playback */
	uint32_t state;
	uint8_t active; /* 0x01 for wave, 0x02 for sound */
	union
	{
		FACTWave *wave;
		FACTSoundInstance sound;
	} playing;
	FACTVariation *playingVariation;

	/* 3D Data */
	uint8_t active3D;
	uint32_t srcChannels;
	uint32_t dstChannels;
	float matrixCoefficients[2 * 8]; /* Stereo input, 7.1 output */

	/* Timer */
	uint32_t start;
	uint32_t elapsed;
};

/* Internal functions */

float FACT_INTERNAL_CalculateAmplitudeRatio(float decibel);
void FACT_INTERNAL_GetNextWave(
	FACTCue *cue,
	FACTSound *sound,
	FACTTrack *track,
	FACTTrackInstance *trackInst,
	FACTEvent *evt,
	FACTEventInstance *evtInst
);
void FACT_INTERNAL_SelectSound(FACTCue *cue);
void FACT_INTERNAL_BeginFadeIn(FACTCue *cue);
void FACT_INTERNAL_BeginFadeOut(FACTCue *cue);

/* FAudio callbacks */

void FACT_INTERNAL_OnProcessingPassStart(FAudioEngineCallback *callback);
void FACT_INTERNAL_OnBufferEnd(FAudioVoiceCallback *callback, void* pContext);
void FACT_INTERNAL_OnStreamEnd(FAudioVoiceCallback *callback);

/* Parsing functions */
uint32_t FACT_INTERNAL_ParseAudioEngine(
	FACTAudioEngine *pEngine,
	const FACTRuntimeParameters *pParams
);
uint32_t FACT_INTERNAL_ParseSoundBank(
	FACTAudioEngine *pEngine,
	const void *pvBuffer,
	uint32_t dwSize,
	FACTSoundBank **ppSoundBank
);
uint32_t FACT_INTERNAL_ParseWaveBank(
	FACTAudioEngine *pEngine,
	FAudioIOStream *io,
	uint16_t isStreaming,
	FACTWaveBank **ppWaveBank
);
