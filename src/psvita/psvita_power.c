/******************************************************************************

	psvita_power.c

	PS Vita power management

******************************************************************************/

#include "emumain.h"
#include "common/power_driver.h"

#include <psp2/power.h>
#include <psp2/kernel/threadmgr.h>
#include <stdlib.h>

typedef struct psvita_power {
} psvita_power_t;

static void *psvita_power_init(void) {
	psvita_power_t *psvita = (psvita_power_t*)calloc(1, sizeof(psvita_power_t));
	
	// Set CPU and GPU to maximum frequency for best performance
	scePowerSetArmClockFrequency(444);
	scePowerSetBusClockFrequency(222);
	scePowerSetGpuClockFrequency(222);
	scePowerSetGpuXbarClockFrequency(166);
	
	return psvita;
}

static void psvita_power_free(void *data) {
	psvita_power_t *psvita = (psvita_power_t*)data;
	free(psvita);
}

static int32_t psvita_batteryLifePercent(void *data) {
	return scePowerGetBatteryLifePercent();
}

static bool psvita_isBatteryCharging(void *data) {
	return scePowerIsBatteryCharging();
}

static void psvita_setCpuClock(void *data, int32_t clock) {
	// Vita CPU frequencies: 41, 83, 111, 166, 222, 266, 333, 366, 444
	scePowerSetArmClockFrequency(clock);
}

static void psvita_setLowestCpuClock(void *data) {
	scePowerSetArmClockFrequency(41);
	scePowerSetBusClockFrequency(41);
	scePowerSetGpuClockFrequency(41);
}

static int32_t psvita_getHighestCpuClock(void *data) {
	return 444;
}

power_driver_t power_psvita = {
	"psvita",
	psvita_power_init,
	psvita_power_free,
	psvita_batteryLifePercent,
	psvita_isBatteryCharging,
	psvita_setCpuClock,
	psvita_setLowestCpuClock,
	psvita_getHighestCpuClock,
};
