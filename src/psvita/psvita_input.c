/******************************************************************************

	psvita_input.c

	PS Vita controller input handling

******************************************************************************/

#include "emumain.h"

#include <psp2/ctrl.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>

typedef struct psvita_input {
	SceCtrlData pad_data;
} psvita_input_t;

static void *psvita_input_init(void) {
	psvita_input_t *psvita = (psvita_input_t*)calloc(1, sizeof(psvita_input_t));
	
	// Initialize controller
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	
	return psvita;
}

static void psvita_input_free(void *data) {
	psvita_input_t *psvita = (psvita_input_t*)data;
	free(psvita);
}

static uint32_t psvita_input_poll(void *data) {
	psvita_input_t *psvita = (psvita_input_t*)data;
	uint32_t buttons = 0;
	
	// Read controller state
	sceCtrlPeekBufferPositive(0, &psvita->pad_data, 1);
	
	// Map PS Vita buttons to platform buttons
	// D-Pad
	if (psvita->pad_data.buttons & SCE_CTRL_UP)
		buttons |= PLATFORM_PAD_UP;
	if (psvita->pad_data.buttons & SCE_CTRL_DOWN)
		buttons |= PLATFORM_PAD_DOWN;
	if (psvita->pad_data.buttons & SCE_CTRL_LEFT)
		buttons |= PLATFORM_PAD_LEFT;
	if (psvita->pad_data.buttons & SCE_CTRL_RIGHT)
		buttons |= PLATFORM_PAD_RIGHT;
	
	// Face buttons
	// Map Cross (X) to B1, Circle (O) to B2, Square to B3, Triangle to B4
	if (psvita->pad_data.buttons & SCE_CTRL_CROSS)
		buttons |= PLATFORM_PAD_B1;
	if (psvita->pad_data.buttons & SCE_CTRL_CIRCLE)
		buttons |= PLATFORM_PAD_B2;
	if (psvita->pad_data.buttons & SCE_CTRL_SQUARE)
		buttons |= PLATFORM_PAD_B3;
	if (psvita->pad_data.buttons & SCE_CTRL_TRIANGLE)
		buttons |= PLATFORM_PAD_B4;
	
	// Shoulder buttons
	if (psvita->pad_data.buttons & SCE_CTRL_LTRIGGER)
		buttons |= PLATFORM_PAD_L;
	if (psvita->pad_data.buttons & SCE_CTRL_RTRIGGER)
		buttons |= PLATFORM_PAD_R;
	
	// System buttons
	if (psvita->pad_data.buttons & SCE_CTRL_SELECT)
		buttons |= PLATFORM_PAD_SELECT;
	if (psvita->pad_data.buttons & SCE_CTRL_START)
		buttons |= PLATFORM_PAD_START;
	
	return buttons;
}

#if (EMU_SYSTEM == MVS)
static uint32_t psvita_input_pollFatfursp(void *data) {
	// Same as regular poll for PS Vita
	return psvita_input_poll(data);
}

static uint32_t psvita_input_pollAnalog(void *data) {
	psvita_input_t *psvita = (psvita_input_t*)data;
	uint32_t buttons = 0;
	
	// Read controller state
	sceCtrlPeekBufferPositive(0, &psvita->pad_data, 1);
	
	// Map analog stick to directional input
	// Analog stick values range from 0-255, with 128 being center
	#define ANALOG_THRESHOLD 64
	
	if (psvita->pad_data.lx < (128 - ANALOG_THRESHOLD))
		buttons |= PLATFORM_PAD_LEFT;
	if (psvita->pad_data.lx > (128 + ANALOG_THRESHOLD))
		buttons |= PLATFORM_PAD_RIGHT;
	if (psvita->pad_data.ly < (128 - ANALOG_THRESHOLD))
		buttons |= PLATFORM_PAD_UP;
	if (psvita->pad_data.ly > (128 + ANALOG_THRESHOLD))
		buttons |= PLATFORM_PAD_DOWN;
	
	// Also include regular buttons
	buttons |= psvita_input_poll(data);
	
	return buttons;
}
#endif

input_driver_t input_psvita = {
	"psvita",
	psvita_input_init,
	psvita_input_free,
	psvita_input_poll,
#if (EMU_SYSTEM == MVS)
	psvita_input_pollFatfursp,
	psvita_input_pollAnalog,
#endif
};
