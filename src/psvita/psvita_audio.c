/******************************************************************************

	psvita_audio.c

	PS Vita audio output using SceAudio

******************************************************************************/

#include "emumain.h"

#include <psp2/audioout.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLE_ALIGN(s) (((s) + 63) & ~63)

typedef struct psvita_audio {
	int port;
	int16_t *audio_buffer;
	uint16_t sample_count;
	uint8_t channels;
} psvita_audio_t;

static void *psvita_audio_init(void) {
	psvita_audio_t *psvita = (psvita_audio_t*)calloc(1, sizeof(psvita_audio_t));
	
	psvita->port = -1;
	psvita->audio_buffer = NULL;
	psvita->sample_count = 0;
	psvita->channels = AUDIO_CHANNELS;
	
	return psvita;
}

static void psvita_audio_free(void *data) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port >= 0) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
	}
	
	if (psvita->audio_buffer) {
		free(psvita->audio_buffer);
		psvita->audio_buffer = NULL;
	}
	
	free(psvita);
}

static int32_t psvita_audio_volumeMax(void *data) {
	return SCE_AUDIO_VOLUME_0DB;
}

static bool psvita_audio_chSRCReserve(void *data, uint16_t samples, int32_t frequency, uint8_t channels) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	// Align sample count
	uint16_t aligned_samples = AUDIO_SAMPLE_ALIGN(samples);
	
	psvita->sample_count = aligned_samples;
	psvita->channels = channels;
	
	// Reserve audio port
	psvita->port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, 
	                                    aligned_samples, 
	                                    frequency, 
	                                    (channels == 2) ? SCE_AUDIO_OUT_MODE_STEREO : SCE_AUDIO_OUT_MODE_MONO);
	
	if (psvita->port < 0) {
		printf("Failed to open audio port: 0x%08X\n", psvita->port);
		return false;
	}
	
	// Allocate audio buffer
	size_t buffer_size = aligned_samples * channels * sizeof(int16_t);
	psvita->audio_buffer = (int16_t*)malloc(buffer_size);
	if (!psvita->audio_buffer) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
		return false;
	}
	
	memset(psvita->audio_buffer, 0, buffer_size);
	
	return true;
}

static bool psvita_audio_chReserve(void *data, uint16_t samplecount, uint8_t channels) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	// Use default frequency of 44100 Hz
	return psvita_audio_chSRCReserve(data, samplecount, 44100, channels);
}

static void psvita_audio_srcOutputBlocking(void *data, int32_t volume, void *buffer, uint32_t size) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port < 0 || !psvita->audio_buffer) {
		return;
	}
	
	// Copy and output audio data
	uint32_t samples = size / (sizeof(int16_t) * psvita->channels);
	if (samples > psvita->sample_count) {
		samples = psvita->sample_count;
	}
	
	memcpy(psvita->audio_buffer, buffer, samples * psvita->channels * sizeof(int16_t));
	
	// Set volume
	int vols[2] = { volume, volume };
	sceAudioOutSetVolume(psvita->port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
	
	// Output audio (blocking)
	sceAudioOutOutput(psvita->port, psvita->audio_buffer);
}

static void psvita_audio_outputPannedBlocking(void *data, int leftvol, int rightvol, void *buffer, uint32_t size) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port < 0 || !psvita->audio_buffer) {
		return;
	}
	
	// Copy and output audio data
	uint32_t samples = size / (sizeof(int16_t) * psvita->channels);
	if (samples > psvita->sample_count) {
		samples = psvita->sample_count;
	}
	
	memcpy(psvita->audio_buffer, buffer, samples * psvita->channels * sizeof(int16_t));
	
	// Set volume for left and right channels
	int vols[2] = { leftvol, rightvol };
	sceAudioOutSetVolume(psvita->port, SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH, vols);
	
	// Output audio (blocking)
	sceAudioOutOutput(psvita->port, psvita->audio_buffer);
}

static void psvita_audio_release(void *data) {
	psvita_audio_t *psvita = (psvita_audio_t*)data;
	
	if (psvita->port >= 0) {
		sceAudioOutReleasePort(psvita->port);
		psvita->port = -1;
	}
	
	if (psvita->audio_buffer) {
		free(psvita->audio_buffer);
		psvita->audio_buffer = NULL;
	}
}

audio_driver_t audio_psvita = {
	"psvita",
	psvita_audio_init,
	psvita_audio_free,
	psvita_audio_volumeMax,
	psvita_audio_chSRCReserve,
	psvita_audio_chReserve,
	psvita_audio_srcOutputBlocking,
	psvita_audio_outputPannedBlocking,
	psvita_audio_release,
};
