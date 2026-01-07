/******************************************************************************

	psvita_platform.c

	PS Vita platform initialization and management

******************************************************************************/

#include "emumain.h"

#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>

typedef struct psvita_platform {
} psvita_platform_t;

static void *psvita_init(void) {
	psvita_platform_t *psvita = (psvita_platform_t*)calloc(1, sizeof(psvita_platform_t));
	option_samplerate = 2; // Default to 44kHz

	return psvita;
}

static void psvita_free(void *data) {
	psvita_platform_t *psvita = (psvita_platform_t*)data;

	free(psvita);
}

static void psvita_main(void *data, int argc, char *argv[]) {
	// psvita_platform_t *psvita = (psvita_platform_t*)data;
}

static bool psvita_startSystemButtons(void *data) {
	return false;
}

static int32_t psvita_getDevkitVersion(void *data) {
	return 0;
}

platform_driver_t platform_psvita = {
	"psvita",
	psvita_init,
	psvita_free,
	psvita_main,
	psvita_startSystemButtons,
	psvita_getDevkitVersion,
};
