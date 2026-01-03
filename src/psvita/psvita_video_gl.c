/******************************************************************************

	psvita_video_gl.c

	PS Vita Video Control Functions using VitaGL (OpenGL ES)

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <string.h>
#include <vitaGL.h>

#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 544

typedef struct psvita_video_gl {
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

	// OpenGL textures
	GLuint gl_texture_scrbitmap;
	GLuint gl_texture_tex_spr0;
	GLuint gl_texture_tex_spr1;
	GLuint gl_texture_tex_spr2;
	GLuint gl_texture_tex_fix;
	
	// Converted texture data (RGBA from indexed)
	uint16_t *rgba_scrbitmap;
	uint16_t *rgba_tex_spr0;
	uint16_t *rgba_tex_spr1;
	uint16_t *rgba_tex_spr2;
	uint16_t *rgba_tex_fix;
} psvita_video_gl_t;

/******************************************************************************
	Helper Functions
******************************************************************************/

static GLuint create_texture(int width, int height) {
	GLuint texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB5_A1, width, height, 0, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, NULL);
	return texture;
}

/******************************************************************************
	Initialization and Cleanup
******************************************************************************/

static void psvita_gl_start(void *data) {
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;

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

	// Allocate RGBA conversion buffers
	psvita->rgba_scrbitmap = (uint16_t*)malloc(scrbitmapSize * sizeof(uint16_t));
	psvita->rgba_tex_spr0 = (uint16_t*)malloc(textureSize * sizeof(uint16_t));
	psvita->rgba_tex_spr1 = (uint16_t*)malloc(textureSize * sizeof(uint16_t));
	psvita->rgba_tex_spr2 = (uint16_t*)malloc(textureSize * sizeof(uint16_t));
	psvita->rgba_tex_fix = (uint16_t*)malloc(textureSize * sizeof(uint16_t));

	// Create OpenGL textures
	psvita->gl_texture_scrbitmap = create_texture(BUF_WIDTH, SCR_HEIGHT);
	psvita->gl_texture_tex_spr0 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->gl_texture_tex_spr1 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->gl_texture_tex_spr2 = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);
	psvita->gl_texture_tex_fix = create_texture(BUF_WIDTH, TEXTURE_HEIGHT);

	ui_init();
}

static void *psvita_gl_init(void)
{
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)calloc(1, sizeof(psvita_video_gl_t));
	psvita->draw_extra_info = false;

	// Initialize VitaGL
	vglInitExtended(0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0x1800000, SCE_GXM_MULTISAMPLE_NONE);
	
	// Set up OpenGL state
	glViewport(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrthof(0, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_BLEND);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	psvita_gl_start(psvita);
	return psvita;
}

static void psvita_gl_exit(psvita_video_gl_t *psvita) {
	if (psvita->gl_texture_scrbitmap) {
		glDeleteTextures(1, &psvita->gl_texture_scrbitmap);
		psvita->gl_texture_scrbitmap = 0;
	}

	if (psvita->gl_texture_tex_spr0) {
		glDeleteTextures(1, &psvita->gl_texture_tex_spr0);
		psvita->gl_texture_tex_spr0 = 0;
	}

	if (psvita->gl_texture_tex_spr1) {
		glDeleteTextures(1, &psvita->gl_texture_tex_spr1);
		psvita->gl_texture_tex_spr1 = 0;
	}

	if (psvita->gl_texture_tex_spr2) {
		glDeleteTextures(1, &psvita->gl_texture_tex_spr2);
		psvita->gl_texture_tex_spr2 = 0;
	}

	if (psvita->gl_texture_tex_fix) {
		glDeleteTextures(1, &psvita->gl_texture_tex_fix);
		psvita->gl_texture_tex_fix = 0;
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

	if (psvita->rgba_scrbitmap) {
		free(psvita->rgba_scrbitmap);
		psvita->rgba_scrbitmap = NULL;
	}

	if (psvita->rgba_tex_spr0) {
		free(psvita->rgba_tex_spr0);
		psvita->rgba_tex_spr0 = NULL;
	}

	if (psvita->rgba_tex_spr1) {
		free(psvita->rgba_tex_spr1);
		psvita->rgba_tex_spr1 = NULL;
	}

	if (psvita->rgba_tex_spr2) {
		free(psvita->rgba_tex_spr2);
		psvita->rgba_tex_spr2 = NULL;
	}

	if (psvita->rgba_tex_fix) {
		free(psvita->rgba_tex_fix);
		psvita->rgba_tex_fix = NULL;
	}
}

static void psvita_gl_free(void *data)
{
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;

	psvita_gl_exit(psvita);
	vglEnd();

	free(psvita);
}

/******************************************************************************
	Video Driver Interface Implementation
******************************************************************************/

static void psvita_gl_setMode(void *data, int mode) {
	// Mode setting not needed for OpenGL
}

static void psvita_gl_setClutBaseAddr(void *data, uint16_t *clut_base) {
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;
	psvita->clut_base = clut_base;
}

static void psvita_gl_waitVsync(void *data) {
	vglSwapBuffers(GL_TRUE);
}

static void psvita_gl_flipScreen(void *data, bool vsync) {
	vglSwapBuffers(vsync ? GL_TRUE : GL_FALSE);
}

static void *psvita_gl_frameAddr(void *data, void *frame, int x, int y) {
	return &((uint16_t*)frame)[y * BUF_WIDTH + x];
}

static void *psvita_gl_workFrame(void *data, enum WorkBuffer buffer) {
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;
	
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

static void psvita_gl_clearScreen(void *data) {
	glClear(GL_COLOR_BUFFER_BIT);
	vglSwapBuffers(GL_FALSE);
}

static void psvita_gl_clearFrame(void *data, void *frame) {
	memset(frame, 0, BUF_WIDTH * SCR_HEIGHT * sizeof(uint16_t));
}

static void psvita_gl_fillFrame(void *data, void *frame, uint32_t color) {
	uint16_t *dst = (uint16_t*)frame;
	uint16_t color16 = (uint16_t)color;
	
	for (int i = 0; i < BUF_WIDTH * SCR_HEIGHT; i++) {
		dst[i] = color16;
	}
}

static void psvita_gl_startWorkFrame(void *data, uint32_t color) {
	glClear(GL_COLOR_BUFFER_BIT);
}

static void psvita_gl_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect) {
	// Transfer is handled during blit operations
}

static void psvita_gl_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_gl_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_gl_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void psvita_gl_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	// Not commonly used in the emulator core
}

static void *psvita_gl_getNativeObjects(void *data, int index) {
	// Return native objects if needed
	return NULL;
}

static void psvita_gl_uploadMem(void *data, enum WorkBuffer buffer) {
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;
	
	GLuint texture = 0;
	uint8_t *src_buffer = NULL;
	uint16_t *rgba_buffer = NULL;
	int width = BUF_WIDTH;
	int height = 0;
	
	switch (buffer) {
		case SCRBITMAP:
			texture = psvita->gl_texture_scrbitmap;
			src_buffer = psvita->scrbitmap;
			rgba_buffer = psvita->rgba_scrbitmap;
			height = SCR_HEIGHT;
			break;
		case TEX_SPR0:
			texture = psvita->gl_texture_tex_spr0;
			src_buffer = psvita->tex_spr0;
			rgba_buffer = psvita->rgba_tex_spr0;
			height = TEXTURE_HEIGHT;
			break;
		case TEX_SPR1:
			texture = psvita->gl_texture_tex_spr1;
			src_buffer = psvita->tex_spr1;
			rgba_buffer = psvita->rgba_tex_spr1;
			height = TEXTURE_HEIGHT;
			break;
		case TEX_SPR2:
			texture = psvita->gl_texture_tex_spr2;
			src_buffer = psvita->tex_spr2;
			rgba_buffer = psvita->rgba_tex_spr2;
			height = TEXTURE_HEIGHT;
			break;
		case TEX_FIX:
			texture = psvita->gl_texture_tex_fix;
			src_buffer = psvita->tex_fix;
			rgba_buffer = psvita->rgba_tex_fix;
			height = TEXTURE_HEIGHT;
			break;
		default:
			return;
	}
	
	if (texture && src_buffer && rgba_buffer && psvita->clut_base) {
		// Convert indexed color to RGB565 using CLUT
		for (int y = 0; y < height; y++) {
			for (int x = 0; x < width; x++) {
				uint8_t index = src_buffer[y * width + x];
				uint16_t color = psvita->clut_base[index];
				rgba_buffer[y * width + x] = color | 0x8000; // Set alpha bit
			}
		}
		
		// Upload to OpenGL texture
		glBindTexture(GL_TEXTURE_2D, texture);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, rgba_buffer);
	}
}

static void psvita_gl_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
	// CLUT is handled during texture upload
}

static void psvita_gl_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t bank_index, uint32_t vertices_count, void *vertices) {
	psvita_video_gl_t *psvita = (psvita_video_gl_t*)data;
	
	GLuint texture = 0;
	
	switch (buffer) {
		case SCRBITMAP:
			texture = psvita->gl_texture_scrbitmap;
			break;
		case TEX_SPR0:
			texture = psvita->gl_texture_tex_spr0;
			break;
		case TEX_SPR1:
			texture = psvita->gl_texture_tex_spr1;
			break;
		case TEX_SPR2:
			texture = psvita->gl_texture_tex_spr2;
			break;
		case TEX_FIX:
			texture = psvita->gl_texture_tex_fix;
			break;
		default:
			return;
	}
	
	if (!texture || !vertices) {
		return;
	}
	
	glBindTexture(GL_TEXTURE_2D, texture);
	
	// Use batched rendering with vertex arrays (OpenGL ES 1.1 style)
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	
	// Build arrays for batch rendering
	struct Vertex *verts = (struct Vertex*)vertices;
	GLfloat *vertex_data = (GLfloat*)malloc(vertices_count * 2 * sizeof(GLfloat));
	GLfloat *texcoord_data = (GLfloat*)malloc(vertices_count * 2 * sizeof(GLfloat));
	
	for (uint32_t i = 0; i < vertices_count; i++) {
		vertex_data[i * 2 + 0] = (GLfloat)verts[i].x;
		vertex_data[i * 2 + 1] = (GLfloat)verts[i].y;
		texcoord_data[i * 2 + 0] = (GLfloat)verts[i].u / (GLfloat)BUF_WIDTH;
		texcoord_data[i * 2 + 1] = (GLfloat)verts[i].v / (GLfloat)((buffer == SCRBITMAP) ? SCR_HEIGHT : TEXTURE_HEIGHT);
	}
	
	glVertexPointer(2, GL_FLOAT, 0, vertex_data);
	glTexCoordPointer(2, GL_FLOAT, 0, texcoord_data);
	
	// Draw all sprites in one batched call
	glDrawArrays(GL_TRIANGLES, 0, vertices_count);
	
	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	
	free(vertex_data);
	free(texcoord_data);
}

/******************************************************************************
	Video Driver Structure
******************************************************************************/

video_driver_t video_psvita = {
	"psvita_gl",
	psvita_gl_init,
	psvita_gl_free,
	psvita_gl_setMode,
	psvita_gl_setClutBaseAddr,
	psvita_gl_waitVsync,
	psvita_gl_flipScreen,
	psvita_gl_frameAddr,
	psvita_gl_workFrame,
	psvita_gl_clearScreen,
	psvita_gl_clearFrame,
	psvita_gl_fillFrame,
	psvita_gl_startWorkFrame,
	psvita_gl_transferWorkFrame,
	psvita_gl_copyRect,
	psvita_gl_copyRectFlip,
	psvita_gl_copyRectRotate,
	psvita_gl_drawTexture,
	psvita_gl_getNativeObjects,
	psvita_gl_uploadMem,
	psvita_gl_uploadClut,
	psvita_gl_blitTexture,
};
