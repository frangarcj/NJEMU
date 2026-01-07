/******************************************************************************

	psvita_video.c

	PS Vita Video Control Functions using vita2d
	
	Optimized implementation using 16-bit Textures (U1U5U5U5_ABGR).
	Matches Desktop SDL_PIXELFORMAT_ABGR1555.
	Performs fast software palette lookup without RGB conversion logic.

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <string.h>
#include <vita2d.h> 

// Set to 1 to enable Render-to-Texture (Better scaling/effects but complex)
// Set to 0 to enable Direct Drawing (Simpler debug mode)
#define USE_RENDER_TARGET 1

typedef struct psvita_video {
	uint16_t *clut_base;

	// Indexed buffers (8-bit)
	uint8_t *scrbitmap;
	uint8_t *tex_spr;
	uint8_t *tex_spr0;
	uint8_t *tex_spr1;
	uint8_t *tex_spr2;
	uint8_t *tex_fix;

	// Vita2D textures (16-bit format)
	vita2d_texture *vita_tex_spr0;
	vita2d_texture *vita_tex_spr1;
	vita2d_texture *vita_tex_spr2;
	vita2d_texture *vita_tex_fix;
	
	// Offscreen Render Target for Scaling
	vita2d_texture *render_target;
	
	bool is_drawing;
} psvita_video_t;

/******************************************************************************
	Initialization
******************************************************************************/

static void psvita_start(void *data) {
	psvita_video_t *psvita = (psvita_video_t*)data;

	size_t scrbitmapSize = BUF_WIDTH * SCR_HEIGHT;
	size_t textureSize = BUF_WIDTH * TEXTURE_HEIGHT;
	
	psvita->scrbitmap = (uint8_t*)malloc(scrbitmapSize);
	memset(psvita->scrbitmap, 0, scrbitmapSize); 
	
	uint8_t *tex_spr = (uint8_t*)malloc(textureSize * 3);
	memset(tex_spr, 0, textureSize * 3);
	psvita->tex_spr = tex_spr;
	psvita->tex_spr0 = tex_spr;
	psvita->tex_spr1 = tex_spr + textureSize;
	psvita->tex_spr2 = tex_spr + textureSize * 2;
	
	psvita->tex_fix = (uint8_t*)malloc(textureSize);
	memset(psvita->tex_fix, 0, textureSize);

	// Use SCE_GXM_TEXTURE_FORMAT_U5U5U5U1_BGRA (1-bit Alpha at MSB)
	// This matches the 0x8000 alpha mask logic.
	SceGxmTextureFormat format = SCE_GXM_TEXTURE_FORMAT_U5U5U5U1_BGRA;
	
	psvita->vita_tex_spr0 = vita2d_create_empty_texture_format(BUF_WIDTH, TEXTURE_HEIGHT, format);
	psvita->vita_tex_spr1 = vita2d_create_empty_texture_format(BUF_WIDTH, TEXTURE_HEIGHT, format);
	psvita->vita_tex_spr2 = vita2d_create_empty_texture_format(BUF_WIDTH, TEXTURE_HEIGHT, format);
	psvita->vita_tex_fix = vita2d_create_empty_texture_format(BUF_WIDTH, TEXTURE_HEIGHT, format);
	
#if USE_RENDER_TARGET
	// Create Render Target (Match PS Vita Screen Size)
	psvita->render_target = vita2d_create_empty_texture_rendertarget(SCR_WIDTH, SCR_HEIGHT, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
	vita2d_texture_set_filters(psvita->render_target, SCE_GXM_TEXTURE_FILTER_POINT, SCE_GXM_TEXTURE_FILTER_POINT);
#else
	psvita->render_target = NULL;
#endif
	
	// Clear all textures to Transparent (0)
	size_t tex_stride = vita2d_texture_get_stride(psvita->vita_tex_spr0);
	size_t tex_bytes = tex_stride * TEXTURE_HEIGHT;
	
	memset(vita2d_texture_get_datap(psvita->vita_tex_spr0), 0, tex_bytes);
	memset(vita2d_texture_get_datap(psvita->vita_tex_spr1), 0, tex_bytes);
	memset(vita2d_texture_get_datap(psvita->vita_tex_spr2), 0, tex_bytes);
	memset(vita2d_texture_get_datap(psvita->vita_tex_fix), 0, tex_bytes);

	ui_init();
}

static void *psvita_init(void)
{
	psvita_video_t *psvita = (psvita_video_t*)calloc(1, sizeof(psvita_video_t));

	vita2d_init();
	vita2d_set_clear_color(RGBA8(0x00, 0x00, 0x00, 0xFF));
	vita2d_set_blend_mode_add(0);

	psvita_start(psvita);
	return psvita;
}

static void psvita_exit(psvita_video_t *psvita) {
	if (psvita->vita_tex_spr0) vita2d_free_texture(psvita->vita_tex_spr0);
	if (psvita->vita_tex_spr1) vita2d_free_texture(psvita->vita_tex_spr1);
	if (psvita->vita_tex_spr2) vita2d_free_texture(psvita->vita_tex_spr2);
	if (psvita->vita_tex_fix) vita2d_free_texture(psvita->vita_tex_fix);
	if (psvita->render_target) vita2d_free_texture(psvita->render_target);

	if (psvita->scrbitmap) free(psvita->scrbitmap);
	if (psvita->tex_spr) free(psvita->tex_spr);
	if (psvita->tex_fix) free(psvita->tex_fix);
}

static void psvita_free(void *data)
{
	psvita_video_t *psvita = (psvita_video_t*)data;
	psvita_exit(psvita);
	vita2d_fini();
	free(psvita);
}

/******************************************************************************
	Video Driver Interface
******************************************************************************/

static void psvita_setMode(void *data, int mode) {
}

static void psvita_setClutBaseAddr(void *data, uint16_t *clut_base) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	psvita->clut_base = clut_base;
}

static void psvita_waitVsync(void *data) {
}

static void psvita_flipScreen(void *data, bool vsync) {

	psvita_video_t *psvita = (psvita_video_t*)data;
	
	// Ensure drawing is ended if it wasn't already (safety)
	if (psvita->is_drawing) {
		vita2d_end_drawing();
		psvita->is_drawing = false;
	}
	
	vita2d_common_dialog_update();
	vita2d_swap_buffers();
}

static void *psvita_frameAddr(void *data, void *frame, int x, int y) {
	return &((uint16_t*)frame)[y * BUF_WIDTH + x];
}

static void *psvita_workFrame(void *data, enum WorkBuffer buffer) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	switch (buffer) {
		case SCRBITMAP: return psvita->scrbitmap;
		case TEX_SPR0:  return psvita->tex_spr0;
		case TEX_SPR1:  return psvita->tex_spr1;
		case TEX_SPR2:  return psvita->tex_spr2;
		case TEX_FIX:   return psvita->tex_fix;
		default:        return NULL;
	}
}

static void psvita_clearScreen(void *data) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	if (!psvita->is_drawing) {
		if (psvita->render_target) {
			// Start drawing to Render Target
			vita2d_start_drawing_advanced(psvita->render_target, 0);
			// Reset clip to ensure full clear
			vita2d_set_region_clip(SCE_GXM_REGION_CLIP_NONE, 0, 0, SCR_WIDTH, SCR_HEIGHT);
		} else {
			vita2d_start_drawing();
		}
		psvita->is_drawing = true;
	}
	vita2d_clear_screen();
	sceClibPrintf("psvita_clearScreen 1\n");
}

static void psvita_clearFrame(void *data, void *frame) {
}

static void psvita_fillFrame(void *data, void *frame, uint32_t color) {
}

static void psvita_startWorkFrame(void *data, uint32_t color) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	// Force Alpha to Opaque (0xFF) to ensure background visibility
	// Assuming color is 0xAABBGGRR or similar where MSB is Alpha.
	
	// Start RT and Clear
	if (psvita->render_target) {
		vita2d_start_drawing_advanced(psvita->render_target, 0);
		// Reset clip to ensure full clear
		vita2d_draw_rectangle(0, 0, SCR_WIDTH, SCR_HEIGHT, color | 0xFF000000);


	} else {
		vita2d_start_drawing();
		vita2d_set_clear_color(color);
		vita2d_clear_screen();
	}
	
	
	psvita->is_drawing = true;
}

static void psvita_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	if (psvita->render_target) {
		// Finish drawing to Render Target
		if (psvita->is_drawing) {
			vita2d_end_drawing();
			psvita->is_drawing = false;
		}

		// Draw Render Target to Screen
		vita2d_start_drawing();
		float sx = (float)src_rect->left;
		float sy = (float)src_rect->top;
		float sw = (float)(src_rect->right - src_rect->left);
		float sh = (float)(src_rect->bottom - src_rect->top);
		
		// Fullscreen Scale
		vita2d_draw_texture_part_scale(psvita->render_target,
			0, 0, // Dest X,Y
			sx, sy, sw, sh, // Src Rect
			960.0f / sw, 544.0f / sh // Scale
		);
		
		vita2d_end_drawing();
	}
}

static void psvita_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void psvita_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void psvita_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void psvita_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void *psvita_getNativeObjects(void *data, int index) {
	return NULL;
}

static void psvita_uploadMem(void *data, enum WorkBuffer buffer) {
}

static void psvita_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
}

static void psvita_getBuffers(psvita_video_t *psvita, enum WorkBuffer buffer, 
                              vita2d_texture **out_tex, uint8_t **out_buf) {
	switch (buffer) {
		case TEX_SPR0:
			*out_tex = psvita->vita_tex_spr0;
			*out_buf = psvita->tex_spr0;
			break;
		case TEX_SPR1:
			*out_tex = psvita->vita_tex_spr1;
			*out_buf = psvita->tex_spr1;
			break;
		case TEX_SPR2:
			*out_tex = psvita->vita_tex_spr2;
			*out_buf = psvita->tex_spr2;
			break;
		case TEX_FIX:
			*out_tex = psvita->vita_tex_fix;
			*out_buf = psvita->tex_fix;
			break;
		default:
			*out_tex = NULL;
			*out_buf = NULL;
	}
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))

static void psvita_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t clut_index, uint32_t vertices_count, void *vertices) {
	psvita_video_t *psvita = (psvita_video_t*)data;
	
	if (!vertices || vertices_count == 0) return;
	
	vita2d_texture *texture = NULL;
	uint8_t *indexed_buf = NULL;
	psvita_getBuffers(psvita, buffer, &texture, &indexed_buf);
	
	if (!texture || !indexed_buf) return;
	
	uint16_t *clut_pal = (uint16_t *)clut;
	if (!clut_pal) return;
	
	// Prepare for software copy/lookup
	struct Vertex *verts = (struct Vertex*)vertices;
	
	// Get texture data as 16-bit
	uint16_t *tex_data = (uint16_t*)vita2d_texture_get_datap(texture);
	unsigned int tex_stride = vita2d_texture_get_stride(texture) / 2; // Stride in 16-bit words
	if (tex_stride == 0) tex_stride = BUF_WIDTH;
	
	// Screen scaling
	float SCALE_X = 1.0f;
	float SCALE_Y = 1.0f;
	float OFFSET_Y = 0.0f;
	
	if (!psvita->render_target) {
		// Direct Draw Mode: Scale to fill screen
		SCALE_X = 960.0f / 304.0f; 
		SCALE_Y = 544.0f / 224.0f;
		// Center X/Y if needed? assuming 0,0 draw origin
	}
	
	// Process each sprite
	for (uint32_t i = 0; i < vertices_count; i += 2) {
		struct Vertex *v0 = &verts[i];
		struct Vertex *v1 = &verts[i + 1];
		
		int u0 = v0->u, v0_v = v0->v;
		int u1 = v1->u, v1_v = v1->v;
		int x0 = v0->x, y0 = v0->y;
		int x1 = v1->x, y1 = v1->y;
		
		int src_x = MIN(u0, u1);
		int src_y = MIN(v0_v, v1_v);
		int src_w = abs(u1 - u0);
		int src_h = abs(v1_v - v0_v);
		
		if (src_w == 0 || src_h == 0) continue;
		
		// Optimization: Check bounds
		if (src_x + src_w > BUF_WIDTH) src_w = BUF_WIDTH - src_x;
		if (src_y + src_h > TEXTURE_HEIGHT) src_h = TEXTURE_HEIGHT - src_y;
		
		// Software Palette Lookup: 8-bit index -> 16-bit pixel
		// No expensive RGB unpacking/repacking.
		// Just Copy and OR with 0x8000 for visibility.
		
		for (int y = 0; y < src_h; y++) {
			int buf_y = src_y + y;
			uint16_t *dst_line = &tex_data[buf_y * tex_stride + src_x];
			uint8_t *src_line = &indexed_buf[buf_y * BUF_WIDTH + src_x];
			
			for (int x = 0; x < src_w; x++) {
				uint8_t idx = src_line[x];
				// Neo Geo uses 16-color sub-palettes. Index 0 of each is transparent.
				// After palette offset, transparent pixels are at 0, 16, 32, 48, etc.
				if ((idx & 0x0F) != 0) {
					// Format is U1U5U5U5_ABGR (Alpha is Bit 15)
					dst_line[x] = clut_pal[idx] | 0x8000;
				} else {
					dst_line[x] = 0x0000; // Transparent (Alpha=0)
				}
			}
		}
		
		// Draw sprite with scaling
		float dst_x = (float)x0 * SCALE_X;
		float dst_y = (float)y0 * SCALE_Y + OFFSET_Y;
		float dst_w = (float)(x1 - x0) * SCALE_X;
		float dst_h = (float)(y1 - y0) * SCALE_Y;
		
		bool flip_h = (u0 > u1);
		bool flip_v = (v0_v > v1_v);
		
		float scale_x = dst_w / (float)src_w;
		float scale_y = dst_h / (float)src_h;
		
		if (flip_h) {
			scale_x = -scale_x;
			dst_x += dst_w;
		}
		if (flip_v) {
			scale_y = -scale_y;
			dst_y += dst_h;
		}
		
		vita2d_draw_texture_part_scale(texture,
			dst_x, dst_y,
			(float)src_x, (float)src_y, (float)src_w, (float)src_h,
			scale_x, scale_y);
	}
}

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
