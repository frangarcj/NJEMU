/******************************************************************************

	psvita_video.c

	PS Vita Video Control Functions using vita2d

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <vita2d.h>

typedef struct psvita_video {
	bool draw_extra_info;
    
    // Base clut starting address
    uint16_t *clut_base;

	// Original buffers containing clut indexes
	uint8_t *scrbitmap;
	uint8_t *tex_spr;
	uint8_t *tex_spr0;
	uint8_t *tex_spr1;
	uint8_t *tex_spr2;
	uint8_t *tex_fix;

	// vita2d textures
	vita2d_texture *vita_texture_scrbitmap;
	vita2d_texture *vita_texture_tex_spr0;
	vita2d_texture *vita_texture_tex_spr1;
	vita2d_texture *vita_texture_tex_spr2;
	vita2d_texture *vita_texture_tex_fix;
} psvita_video_t;

/******************************************************************************
	Helper Functions
******************************************************************************/

static vita2d_texture *create_texture(int width, int height) {
	// Create texture in ABGR1555 format (compatible with the emulator)
	return vita2d_create_empty_texture_format(width, height, SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB);
}

/******************************************************************************
	Initialization and Cleanup
******************************************************************************/

static void psvita_start(void *data) {
	psvita_video_t *psvita = (psvita_video_t*)data;

	// Allocate original buffers containing clut indexes
	size_t scrbitmapSize = BUF_WIDTH * SCR_HEIGHT;
	size_t textureSize = BUF_WIDTH * TEXTURE_HEIGHT;
	psvita->scrbitmap = (uint8_t*)malloc(scrbitmapSize);
	uint8_t *tex_spr = (uint8_t*)malloc(textureSize * 3);
	psvita->tex_spr = tex_spr;
	psvita->tex_spr0 = tex_spr;
	psvita->tex_spr1 = tex_spr + textureSize;
	psvita->tex_spr2 = tex_spr + textureSize * 2;
	psvita->tex_fix = (uint8_t*)malloc(textureSize);

	// Create vita2d textures
	psvita->vita_texture_scrbitmap = create_texture(BUF_WIDTH, SCR_HEIGHT);
	psvita->vita_texture_tex_spr0 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->vita_texture_tex_spr1 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->vita_texture_tex_spr2 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->vita_texture_tex_fix = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);

	if (!psvita->vita_texture_scrbitmap || !psvita->vita_texture_tex_spr0 ||
	    !psvita->vita_texture_tex_spr1 || !psvita->vita_texture_tex_spr2 ||
	    !psvita->vita_texture_tex_fix) {
		printf("Failed to create vita2d textures\n");
		return;
	}

	ui_init();
}

static void *psvita_init(void)
{
	psvita_video_t *psvita = (psvita_video_t*)calloc(1, sizeof(psvita_video_t));
	psvita->draw_extra_info = false;

	// Initialize vita2d
	vita2d_init();
	vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));

	psvita_start(psvita);
	return psvita;
}

static void psvita_exit(psvita_video_t *psvita) {
	if (psvita->vita_texture_scrbitmap) {
		vita2d_free_texture(psvita->vita_texture_scrbitmap);
		psvita->vita_texture_scrbitmap = NULL;
	}

	if (psvita->vita_texture_tex_spr0) {
		vita2d_free_texture(psvita->vita_texture_tex_spr0);
		psvita->vita_texture_tex_spr0 = NULL;
	}

	if (psvita->vita_texture_tex_spr1) {
		vita2d_free_texture(psvita->vita_texture_tex_spr1);
		psvita->vita_texture_tex_spr1 = NULL;
	}

	if (psvita->vita_texture_tex_spr2) {
		vita2d_free_texture(psvita->vita_texture_tex_spr2);
		psvita->vita_texture_tex_spr2 = NULL;
	}

	if (psvita->vita_texture_tex_fix) {
		vita2d_free_texture(psvita->vita_texture_tex_fix);
		psvita->vita_texture_tex_fix = NULL;
	}

	if (psvita->scrbitmap) {
		free(psvita->scrbitmap);
		psvita->scrbitmap = NULL;
	}

	if (psvita->tex_spr) {
		free(psvita->tex_spr);
		psvita->tex_spr = NULL;
		psvita->tex_spr0 = NULL;
		psvita->tex_spr1 = NULL;
		psvita->tex_spr2 = NULL;
	}

	if (psvita->tex_fix) {
		free(psvita->tex_fix);
		psvita->tex_fix = NULL;
	}
}

static void psvita_free(void *data)
{
	psvita_video_t *psvita = (psvita_video_t*)data;

	psvita_exit(psvita);

	vita2d_fini();

	free(psvita);
}

/******************************************************************************
	Video Driver Interface Implementation
******************************************************************************/

static void psvita_setMode(void *data, int mode) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	// Mode setting not needed for vita2d
}

static void psvita_setClutBaseAddr(void *data, uint16_t *clut_base) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	psvita->clut_base = clut_base;
}

static void psvita_waitVsync(void *data) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	vita2d_wait_rendering_done();
}

static void psvita_flipScreen(void *data, bool vsync) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	vita2d_swap_buffers();
}

static void *psvita_frameAddr(void *data, void *frame, int x, int y) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	return &((uint16_t*)frame)[y * BUF_WIDTH + x];
}

static void *psvita_workFrame(void *data, enum WorkBuffer buffer) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	switch (buffer) {
		case SCRBITMAP:
			return psvita->scrbitmap;
		case TEX_SPR0:
			return psvita->tex_spr0;
		case TEX_SPR1:
			return psvita->tex_spr1;
		case TEX_SPR2:
			return psvita->tex_spr2;
		case TEX_FIX:
			return psvita->tex_fix;
		default:
			return NULL;
	}
}

static void psvita_clearScreen(void *data) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	vita2d_start_drawing();
	vita2d_clear_screen();
	vita2d_end_drawing();
}

static void psvita_clearFrame(void *data, void *frame) {
	// Clear frame by memset
	memset(frame, 0, BUF_WIDTH * SCR_HEIGHT * sizeof(uint16_t));
}

static void psvita_fillFrame(void *data, void *frame, uint32_t color) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	uint16_t *dst = (uint16_t*)frame;
	uint16_t color16 = (uint16_t)color;
	
	for (int i = 0; i < BUF_WIDTH * SCR_HEIGHT; i++) {
		dst[i] = color16;
	}
}

static void psvita_startWorkFrame(void *data, uint32_t color) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	vita2d_start_drawing();
	vita2d_clear_screen();
}

static void psvita_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	// Transfer is handled during blit operations
}

static void psvita_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void *psvita_getNativeObjects(void *data, int index) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	// Return native objects if needed
	return NULL;
}

static void psvita_uploadMem(void *data, enum WorkBuffer buffer) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	// Upload texture data from CPU buffer to GPU texture
	vita2d_texture *texture = NULL;
	uint8_t *src_buffer = NULL;
	
	switch (buffer) {
		case SCRBITMAP:
			texture = psvita->vita_texture_scrbitmap;
			src_buffer = psvita->scrbitmap;
			break;
		case TEX_SPR0:
			texture = psvita->vita_texture_tex_spr0;
			src_buffer = psvita->tex_spr0;
			break;
		case TEX_SPR1:
			texture = psvita->vita_texture_tex_spr1;
			src_buffer = psvita->tex_spr1;
			break;
		case TEX_SPR2:
			texture = psvita->vita_texture_tex_spr2;
			src_buffer = psvita->tex_spr2;
			break;
		case TEX_FIX:
			texture = psvita->vita_texture_tex_fix;
			src_buffer = psvita->tex_fix;
			break;
		default:
			return;
	}
	
	if (texture && src_buffer && psvita->clut_base) {
		// Convert indexed color to RGB using CLUT
		uint16_t *dst = (uint16_t*)vita2d_texture_get_datap(texture);
		int stride = vita2d_texture_get_stride(texture) / 2; // Convert bytes to uint16_t
		int width = BUF_WIDTH;
		int height = (buffer == SCRBITMAP) ? SCR_HEIGHT : TEXTURE_HEIGHT;
		
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uint8_t index = src_buffer[y * width + x];
				uint16_t color = psvita->clut_base[index];
				dst[y * stride + x] = color;
			}
		}
	}
}

static void psvita_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	// CLUT is handled during texture upload
}

static void psvita_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t bank_index, uint32_t vertices_count, void *vertices) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	// Get the appropriate texture
	vita2d_texture *texture = NULL;
	
	switch (buffer) {
		case SCRBITMAP:
			texture = psvita->vita_texture_scrbitmap;
			break;
		case TEX_SPR0:
			texture = psvita->vita_texture_tex_spr0;
			break;
		case TEX_SPR1:
			texture = psvita->vita_texture_tex_spr1;
			break;
		case TEX_SPR2:
			texture = psvita->vita_texture_tex_spr2;
			break;
		case TEX_FIX:
			texture = psvita->vita_texture_tex_fix;
			break;
		default:
			return;
	}
	
	if (!texture || !vertices) {
		return;
	}
	
	// Draw each vertex quad
	struct Vertex *verts = (struct Vertex*)vertices;
	
	for (uint32_t i = 0; i < vertices_count; i += 2) {
		struct Vertex *v0 = &verts[i];
		struct Vertex *v1 = &verts[i + 1];
		
		// Calculate source rectangle (UV coords)
		int src_x = v0->u;
		int src_y = v0->v;
		int src_w = v1->u - v0->u;
		int src_h = v1->v - v0->v;
		
		// Calculate destination rectangle (screen coords)
		int dst_x = v0->x;
		int dst_y = v0->y;
		int dst_w = v1->x - v0->x;
		int dst_h = v1->y - v0->y;
		
		// Draw the texture portion
		vita2d_draw_texture_part_scale(texture, dst_x, dst_y,
		                                src_x, src_y, src_w, src_h,
		                                (float)dst_w / (float)src_w,
		                                (float)dst_h / (float)src_h);
	}
	
	vita2d_end_drawing();
}

/******************************************************************************
	Video Driver Structure
******************************************************************************/

video_driver_t video_psvita = {
	"psvita",
	psvita_init,
	psvita_free,
	psvita_setMode,
	psvita_setClutBaseAddr,
	psvita_waitVsync,
	psvita_flipScreen,
	psvita_frameAddr,
	psvita_workFrame,
	psvita_clearScreen,
	psvita_clearFrame,
	psvita_fillFrame,
	psvita_startWorkFrame,
	psvita_transferWorkFrame,
	psvita_copyRect,
	psvita_copyRectFlip,
	psvita_copyRectRotate,
	psvita_drawTexture,
	psvita_getNativeObjects,
	psvita_uploadMem,
	psvita_uploadClut,
	psvita_blitTexture,
};
