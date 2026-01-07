/******************************************************************************

	psvita.h

	PS Vita platform definitions

******************************************************************************/

#ifndef PSVITA_H
#define PSVITA_H

#ifdef USE_SCE_CLIB_PRINTF
#include <psp2/kernel/clib.h>
#define printf sceClibPrintf
#define vprintf sceClibVprintf
#define msg_printf printf
#endif

#define SCR_WIDTH			960
#define SCR_HEIGHT			544
#define BUF_WIDTH			512

#define REFRESH_RATE		(59.940059)		// Standard PS Vita refresh rate
 
#ifndef PATH_MAX
#define PATH_MAX			4096
#endif
 
#define FONTSIZE			14

#define SCROLLH_MAX_HEIGHT	192

#define GET_TEX_PTR(base, idx, tw, th) \
	(&(base)[((idx) / (BUF_WIDTH / (tw))) * (th) * BUF_WIDTH + ((idx) % (BUF_WIDTH / (tw))) * (tw)])

#endif /* PSVITA_H */
