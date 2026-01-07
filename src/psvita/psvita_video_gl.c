/******************************************************************************

	psvita_video_gl.c

	PS Vita Video Control Functions using VitaGL (OpenGL)
	
	Optimized implementation using GLSL Shaders for Paletted Textures.
	Uses vertex arrays (glDrawArrays) and explicit attributes to avoid 
	legacy fixed-function pipeline issues.

******************************************************************************/

#include "emumain.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vitaGL.h>

#define USE_RENDER_TARGET 1

// Helper struct matching the layout from common/video_driver.h
struct Vertex {
	uint16_t u, v;
	uint16_t color;
	int16_t x, y, z;
};

typedef struct psvita_gl_video {
	uint16_t *clut_base;
	void *last_clut; // Cache for palette upload optimization

	// Indexed buffers (8-bit)
	uint8_t *scrbitmap;
	uint8_t *tex_spr;
	uint8_t *tex_spr0;
	uint8_t *tex_spr1;
	uint8_t *tex_spr2;
	uint8_t *tex_fix;

	// OpenGL textures
	GLuint gl_tex_spr0;
	GLuint gl_tex_spr1;
	GLuint gl_tex_spr2;
	GLuint gl_tex_fix;
	
	GLuint gl_tex_palette; // Texture to hold the CLUT (256x1)
	
	// Shader
	GLuint program;
	GLint u_texture;
	GLint u_palette;
	GLint u_clut_offset; // New uniform for Palette Row
	// Matrix uniform
	GLint u_mvp;
	
	// RGBA buffer for palette conversion (small, 256 entries)
	uint32_t palette_buffer[256];
	
	// FBO
	GLuint fbo;
	GLuint fbo_tex;
	
	// Copy Shader
	GLuint prog_copy;
	GLint u_copy_tex;
	GLint u_copy_mvp;
	
	// Pre-allocated vertex buffer for drawing
	float *draw_buffer; 
	size_t draw_buffer_cap;
} psvita_gl_video_t;

// Modern GLSL ES 1.0 style
const char *vertex_shader_source =
	"uniform mat4 uMVP;\n"
	"attribute vec2 aPosition;\n"
	"attribute vec2 aTexCoord;\n"
	"varying vec2 vTexCoord;\n"
	"void main() {\n"
	"  gl_Position = uMVP * vec4(aPosition, 0.0, 1.0);\n"
	"  vTexCoord = aTexCoord;\n"
	"}\n";

const char *fragment_shader_source =
	"uniform sampler2D uTexture;\n"
	"uniform sampler2D uPalette;\n"
	"varying vec2 vTexCoord;\n"
	"void main() {\n"
	"  float index = texture2D(uTexture, vTexCoord).r;\n"
	"  // Sample from row 0. X = index (0..1), Y = 0.5 (middle of first row)\n"
	"  vec4 color = texture2D(uPalette, vec2(index, 0.5/256.0));\n"
	"  gl_FragColor = color;\n"
	"}\n";

const char *copy_fragment_shader_source =
	"uniform sampler2D uTexture;\n"
	"varying vec2 vTexCoord;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(uTexture, vTexCoord);\n"
	"}\n";

static GLuint compile_shader(GLenum type, const char *source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, NULL);
	glCompileShader(shader);
	
	GLint compiled;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
	if (!compiled) {
		char log[512];
		glGetShaderInfoLog(shader, sizeof(log), NULL, log);
		printf("Shader compilation failed: %s\n", log);
		// Output source for debugging
		printf("Source:\n%s\n", source);
		return 0;
	}
	return shader;
}

static void psvita_gl_start(void *data) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;

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
	
	// Alloc draw buffer (initial size)
	psvita->draw_buffer_cap = 4096 * 4 * 4; // floats
	psvita->draw_buffer = (float*)malloc(psvita->draw_buffer_cap * sizeof(float));

	// Create OpenGL textures
	glGenTextures(1, &psvita->gl_tex_spr0);
	glGenTextures(1, &psvita->gl_tex_spr1);
	glGenTextures(1, &psvita->gl_tex_spr2);
	glGenTextures(1, &psvita->gl_tex_fix);
	glGenTextures(1, &psvita->gl_tex_palette);

	// Configure Sprite/Fix Textures (LUMINANCE / Index)
	GLuint textures[] = {psvita->gl_tex_spr0, psvita->gl_tex_spr1, psvita->gl_tex_spr2, psvita->gl_tex_fix};
	for (int i = 0; i < 4; i++) {
		glBindTexture(GL_TEXTURE_2D, textures[i]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, BUF_WIDTH, TEXTURE_HEIGHT, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
	}
	
	// Configure Palette Texture
	glBindTexture(GL_TEXTURE_2D, psvita->gl_tex_palette);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	// Create Shader Program
	psvita->program = glCreateProgram();
	GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_shader_source);
	GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_source);
	glAttachShader(psvita->program, vs);
	glAttachShader(psvita->program, fs);
	
	// Bind attributes explicitly BEFORE linking
	glBindAttribLocation(psvita->program, 0, "aPosition");
	glBindAttribLocation(psvita->program, 1, "aTexCoord");
	
	glLinkProgram(psvita->program);
	
	// Check link status
	GLint linked;
	glGetProgramiv(psvita->program, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[512];
		glGetProgramInfoLog(psvita->program, sizeof(log), NULL, log);
		printf("Shader linking failed: %s\n", log);
	}
	
	// Get Uniform Locations
	psvita->u_texture = glGetUniformLocation(psvita->program, "uTexture");
	psvita->u_palette = glGetUniformLocation(psvita->program, "uPalette");
	psvita->u_clut_offset = glGetUniformLocation(psvita->program, "uClutOffset");
	psvita->u_mvp = glGetUniformLocation(psvita->program, "uMVP");

#if USE_RENDER_TARGET
	// --- Init Copy Shader ---
	psvita->prog_copy = glCreateProgram();
	GLuint fs_copy = compile_shader(GL_FRAGMENT_SHADER, copy_fragment_shader_source);
	glAttachShader(psvita->prog_copy, vs); // Reuse Vertex Shader
	glAttachShader(psvita->prog_copy, fs_copy);
	
	glBindAttribLocation(psvita->prog_copy, 0, "aPosition");
	glBindAttribLocation(psvita->prog_copy, 1, "aTexCoord");
	
	glLinkProgram(psvita->prog_copy);
	glGetProgramiv(psvita->prog_copy, GL_LINK_STATUS, &linked);
	if (!linked) printf("Copy Shader link failed\n");
	
	psvita->u_copy_tex = glGetUniformLocation(psvita->prog_copy, "uTexture");
	psvita->u_copy_mvp = glGetUniformLocation(psvita->prog_copy, "uMVP");
	
	// --- Init FBO ---
	glGenTextures(1, &psvita->fbo_tex);
	glBindTexture(GL_TEXTURE_2D, psvita->fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 960, 544, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	
	glGenFramebuffers(1, &psvita->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, psvita->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, psvita->fbo_tex, 0);
	
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf("FBO Failed\n");
	}
	
	glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
#endif

	ui_init();
}

static void *psvita_gl_init(void)
{
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)calloc(1, sizeof(psvita_gl_video_t));

	// Initialize VitaGL with larger memory (24MB)
	vglInit(0x1800000);  
	vglWaitVblankStart(GL_TRUE);
	
	// Setup OpenGL state
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glEnable(GL_BLEND);
	// Inverted blending like desktop SDL: bit15=1 -> transparent, bit15=0 -> visible
	glBlendFunc(GL_ONE_MINUS_SRC_ALPHA, GL_SRC_ALPHA);
	glDisable(GL_DEPTH_TEST); // Essential for correct transparency layering
	
	// Setup 2D orthographic projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 960, 544, 0, -1, 1);  // Vita screen coordinates
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	
	// Enable Vertex Arrays
	glEnableClientState(GL_VERTEX_ARRAY); // For compatibility if mixed?
	// We will use enableVertexAttribArray later
	
	glEnable(GL_TEXTURE_2D);

	psvita_gl_start(psvita);
	return psvita;
}

static void psvita_gl_exit(psvita_gl_video_t *psvita) {
	if (psvita->gl_tex_spr0) glDeleteTextures(1, &psvita->gl_tex_spr0);
	if (psvita->gl_tex_spr1) glDeleteTextures(1, &psvita->gl_tex_spr1);
	if (psvita->gl_tex_spr2) glDeleteTextures(1, &psvita->gl_tex_spr2);
	if (psvita->gl_tex_fix) glDeleteTextures(1, &psvita->gl_tex_fix);
	if (psvita->gl_tex_palette) glDeleteTextures(1, &psvita->gl_tex_palette);
	
	if (psvita->program) glDeleteProgram(psvita->program);

	if (psvita->scrbitmap) free(psvita->scrbitmap);
	if (psvita->tex_spr) free(psvita->tex_spr);
	if (psvita->tex_fix) free(psvita->tex_fix);
	if (psvita->draw_buffer) free(psvita->draw_buffer);
}

static void psvita_gl_free(void *data)
{
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	psvita_gl_exit(psvita);
	free(psvita);
}

/******************************************************************************
	Video Driver Interface
******************************************************************************/

static void psvita_gl_setMode(void *data, int mode) {
}

static void psvita_gl_setClutBaseAddr(void *data, uint16_t *clut_base) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	psvita->clut_base = clut_base;
}

static void psvita_gl_waitVsync(void *data) {
}

static void psvita_gl_flipScreen(void *data, bool vsync) {
	vglSwapBuffers(GL_TRUE);
}

static void *psvita_gl_frameAddr(void *data, void *frame, int x, int y) {
	return &((uint16_t*)frame)[y * BUF_WIDTH + x];
}

static void *psvita_gl_workFrame(void *data, enum WorkBuffer buffer) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	
	switch (buffer) {
		case SCRBITMAP: return psvita->scrbitmap;
		case TEX_SPR0:  return psvita->tex_spr0;
		case TEX_SPR1:  return psvita->tex_spr1;
		case TEX_SPR2:  return psvita->tex_spr2;
		case TEX_FIX:   return psvita->tex_fix;
		default:        return NULL;
	}
}

static void psvita_gl_clearScreen(void *data) {
	glClear(GL_COLOR_BUFFER_BIT);
}

static void psvita_gl_clearFrame(void *data, void *frame) {
}

static void psvita_gl_fillFrame(void *data, void *frame, uint32_t color) {
}

static void psvita_gl_startWorkFrame(void *data, uint32_t color) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
#if USE_RENDER_TARGET
	glBindFramebuffer(GL_FRAMEBUFFER, psvita->fbo);
#else
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
#endif
	glViewport(0, 0, 960, 544);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Opaque Black
	glClear(GL_COLOR_BUFFER_BIT);
}

static void psvita_gl_transferWorkFrame(void *data, RECT *src_rect, RECT *dst_rect) {
#if USE_RENDER_TARGET
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;

	glBindFramebuffer(GL_FRAMEBUFFER, 0); // Back to Screen
	glViewport(0, 0, 960, 544);
	// glClear(GL_COLOR_BUFFER_BIT); // Optional if we cover fullscreen

	glUseProgram(psvita->prog_copy);

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, psvita->fbo_tex);
	glUniform1i(psvita->u_copy_tex, 0);

	// MVP Matrix (Ortho 0..960, 544..0)
	// We can reuse the standard GL matrix if it wasn't changed, or upload identity and use -1..1 coords.
	// But our vertex shader expects uMVP.
	// Let's compute simple Ortho matrix manually or use glGetFloatv(GL_MODELVIEW_MATRIX) if possible?
	// VitaGL maintains state.
	// But we are using shaders with `uniform mat4 uMVP`.
	// We need to upload it.
	// Simple Ortho Matrix for 960x544:
	// 2/w, 0, 0, -1
	// 0, -2/h, 0, 1
	// ...
	// Easier: Just draw -1..1 quad and use Identity matrix for uMVP?
	// The Vertex Shader: `gl_Position = uMVP * vec4(aPosition, 0.0, 1.0);`
	// If I pass Identity to uMVP.
	// And I submit vertices as -1..1.
	// Then it covers screen.
	
	float identity[16] = {
		1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1
	};
	glUniformMatrix4fv(psvita->u_copy_mvp, 1, GL_FALSE, identity);
	
	// UV Calculation based on src_rect (Game area in FBO)
	// OpenGL FBO Origin (0,0) is usually Bottom-Left?
	// But our glOrtho(.. 0) drew to "Top" which maps to High V typically.
	// So we invert V to access the Top part of the texture.
	float u0 = (float)src_rect->left / 960.0f;
	float v0 = 1.0f - (float)src_rect->top / 544.0f;     // Top Line (High V)
	float u1 = (float)src_rect->right / 960.0f;
	float v1 = 1.0f - (float)src_rect->bottom / 544.0f;  // Bottom Line (Lower V)
	
	// Fullscreen Quad (-1..1)
	float vertices[] = {
		-1.0f,  1.0f,  u0, v0, // Top Left (High V)
		 1.0f,  1.0f,  u1, v0, // Top Right
		-1.0f, -1.0f,  u0, v1, // Bottom Left (Low V)
		 1.0f, -1.0f,  u1, v1  // Bottom Right
	};
	
	// Draw
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[0]);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
	
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
#endif
	// Disable/Cleanup?
	// Restore Program? Next frame starts init again via blit.
}



static void psvita_gl_copyRectFlip(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void psvita_gl_copyRectRotate(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void psvita_gl_drawTexture(void *data, uint32_t src_fmt, uint32_t dst_fmt, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
}

static void *psvita_gl_getNativeObjects(void *data, int index) {
	return NULL;
}

static GLuint psvita_gl_getTexture(psvita_gl_video_t *psvita, enum WorkBuffer buffer, uint8_t **out_buf);

static void psvita_gl_copyRect(void *data, void *src, void *dst, RECT *src_rect, RECT *dst_rect) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	
	// Identify destination buffer to trigger upload
	GLuint texture = 0;
	if (dst == psvita->tex_spr0) texture = psvita->gl_tex_spr0;
	else if (dst == psvita->tex_spr1) texture = psvita->gl_tex_spr1;
	else if (dst == psvita->tex_spr2) texture = psvita->gl_tex_spr2;
	else if (dst == psvita->tex_fix) texture = psvita->gl_tex_fix;
	
	if (texture && dst_rect) {
		int w = dst_rect->right - dst_rect->left;
		int h = dst_rect->bottom - dst_rect->top;
		
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, texture);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, BUF_WIDTH);
		// Assuming dst points to the start of the buffer, we map offset using dst_rect
		// indexed_buf is base pointer (dst).
		uint8_t *buf = (uint8_t*)dst;
		glTexSubImage2D(GL_TEXTURE_2D, 0, dst_rect->left, dst_rect->top, w, h, GL_LUMINANCE, GL_UNSIGNED_BYTE, &buf[dst_rect->top * BUF_WIDTH + dst_rect->left]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

static void psvita_gl_uploadMem(void *data, enum WorkBuffer buffer) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	uint8_t *indexed_buf = NULL;
	GLuint texture = psvita_gl_getTexture(psvita, buffer, &indexed_buf);
	if (!texture || !indexed_buf) return;
	
	// Upload FULL texture
	// We assume TEXTURE_HEIGHT is valid from shared headers. 

	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, BUF_WIDTH);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, BUF_WIDTH, TEXTURE_HEIGHT, GL_LUMINANCE, GL_UNSIGNED_BYTE, indexed_buf);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void psvita_gl_uploadClut(void *data, uint16_t *bank, uint8_t bank_index) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	if (!bank) return;
	
	// Convert CLUT bank to RGBA
	for (int i = 0; i < 256; i++) {
		uint16_t col15 = bank[i];
		uint8_t r = (col15 & 0x1F) << 3;
		uint8_t g = ((col15 >> 5) & 0x1F) << 3;
		uint8_t b = ((col15 >> 10) & 0x1F) << 3;
		// Inverted alpha
		uint8_t a = (col15 & 0x8000) ? 255 : 0;
		// Expansion
		r |= (r >> 5); g |= (g >> 5); b |= (b >> 5);
		psvita->palette_buffer[i] = (a << 24) | (b << 16) | (g << 8) | r;
	}
	
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, psvita->gl_tex_palette);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, bank_index, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, psvita->palette_buffer);
}

static GLuint psvita_gl_getTexture(psvita_gl_video_t *psvita, enum WorkBuffer buffer, uint8_t **out_buf) {
	switch (buffer) {
		case TEX_SPR0:
			*out_buf = psvita->tex_spr0;
			return psvita->gl_tex_spr0;
		case TEX_SPR1:
			*out_buf = psvita->tex_spr1;
			return psvita->gl_tex_spr1;
		case TEX_SPR2:
			*out_buf = psvita->tex_spr2;
			return psvita->gl_tex_spr2;
		case TEX_FIX:
			*out_buf = psvita->tex_fix;
			return psvita->gl_tex_fix;
		default:
			*out_buf = NULL;
			return 0;
	}
}

static void psvita_gl_blitTexture(void *data, enum WorkBuffer buffer, void *clut, uint8_t clut_index, uint32_t vertices_count, void *vertices) {
	psvita_gl_video_t *psvita = (psvita_gl_video_t*)data;
	
	if (!vertices || vertices_count == 0) return;
	
	uint8_t *indexed_buf = NULL;
	GLuint texture = psvita_gl_getTexture(psvita, buffer, &indexed_buf);
	
	if (!texture || !indexed_buf) return;
	
	uint16_t *clut_pal = (uint16_t *)clut;
	if (!clut_pal) return;
	
	struct Vertex *verts = (struct Vertex*)vertices;
	
	// 1. Optimized Palette Upload
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, psvita->gl_tex_palette);
	
	if (psvita->last_clut != clut) {
		uint16_t *clut_pal = (uint16_t *)clut;
		for (int i = 0; i < 256; i++) {
			uint16_t col15 = clut_pal[i];
			uint8_t r = (col15 & 0x1F) << 3;
			uint8_t g = ((col15 >> 5) & 0x1F) << 3;
			uint8_t b = ((col15 >> 10) & 0x1F) << 3;
			uint8_t a = (col15 & 0x8000) ? 255 : 0;
			r |= (r >> 5); g |= (g >> 5); b |= (b >> 5);
			psvita->palette_buffer[i] = (a << 24) | (b << 16) | (g << 8) | r;
		}
		// Upload to Row 0 (we use 256x256 texture now but only 1 row)
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, psvita->palette_buffer);
		psvita->last_clut = clut;
	}

	// 2. Texture Upload (Handled by psvita_gl_uploadRect via Cache Miss)
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, texture);

	// 3. Setup Shader and Drawing
	glUseProgram(psvita->program);
	glUniform1i(psvita->u_texture, 0);
	glUniform1i(psvita->u_palette, 1);
	
	// Upload Ortho Matrix (960x544, 0,0 top-left)
	// glOrtho(0, 960, 544, 0, -1, 1)
	// Column-major
	float mvp[16] = {
		2.0f/960.0f, 0.0f,        0.0f, 0.0f,
		0.0f,       -2.0f/544.0f, 0.0f, 0.0f,
		0.0f,        0.0f,       -1.0f, 0.0f,
		-1.0f,       1.0f,        0.0f, 1.0f
	};
	glUniformMatrix4fv(psvita->u_mvp, 1, GL_FALSE, mvp);
	
	// Screen scaling (Direct Draw Mode)
#if USE_RENDER_TARGET
	const float SCALE_X = 1.0f;  
	const float SCALE_Y = 1.0f;  
	const float OFFSET_Y = 0.0f;
#else
	// 304x224 -> 960x544
	// Width: 304 * 3.15 = 957.
	// Height: 224 * 2.42 = 542.
	// Integer scale preferred?
	// 3x Scale = 912. (Centered: 24px margins).
	// 2x Scale Y = 448 (Centered: 48px margins).
	// Or Stretch?
	// User wants FULL SCREEN potentially.
	// Let's use the explicit scaling factors from before.
	const float SCALE_X = 960.0f / 320.0f; // Stretch full width
	const float SCALE_Y = 544.0f / 224.0f; // Stretch full height
	const float OFFSET_Y = 0.0f;
#endif
	
	float tex_w = (float)BUF_WIDTH;
	float tex_h = (float)TEXTURE_HEIGHT;
    // FIXED BUFFER STRATEGY: 
    // Instead of reallocating (which crashes if psvita-gl implementation is fragile),
    // we flush the buffer if it's full.
    
    // Each quad uses 16 floats (4 verts * 4 floats).
    // needed = (vertices_count / 2) * 16
    
    float *buf = psvita->draw_buffer;
    int idx = 0;
    int batch_quads = 0;
    
	for (uint32_t i = 0; i < vertices_count; i += 2) {
		// Check capacity before adding a quad (16 floats)
		if ((idx + 16) > psvita->draw_buffer_cap) {
			// Flush current batch
			if (batch_quads > 0) {
				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), buf);
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), buf + 2);
				glDrawArrays(GL_QUADS, 0, batch_quads * 4);
				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
			}
			// Reset buffer
			idx = 0;
			batch_quads = 0;
		}

		struct Vertex *v0 = &verts[i];
		struct Vertex *v1 = &verts[i + 1];
		
		float u0 = (float)v0->u / tex_w;
		float v0_t = (float)v0->v / tex_h;
		float u1 = (float)v1->u / tex_w;
		float v1_t = (float)v1->v / tex_h;
		
		float x0 = (float)v0->x * SCALE_X;
		float y0 = (float)v0->y * SCALE_Y + OFFSET_Y;
		float x1 = (float)v1->x * SCALE_X;
		float y1 = (float)v1->y * SCALE_Y + OFFSET_Y;
		
		// Quad logic:
		// V0 (x0, y0) - (u0, v0_t)
		// V1 (x1, y0) - (u1, v0_t)
		// V2 (x1, y1) - (u1, v1_t)
		// V3 (x0, y1) - (u0, v1_t)
		
		// Vertex 0
		buf[idx++] = x0; buf[idx++] = y0;
		buf[idx++] = u0; buf[idx++] = v0_t;
		
		// Vertex 1
		buf[idx++] = x1; buf[idx++] = y0;
		buf[idx++] = u1; buf[idx++] = v0_t;
		
		// Vertex 2
		buf[idx++] = x1; buf[idx++] = y1;
		buf[idx++] = u1; buf[idx++] = v1_t;
		
		// Vertex 3
		buf[idx++] = x0; buf[idx++] = y1;
		buf[idx++] = u0; buf[idx++] = v1_t;
		
		batch_quads++;
	}
	
	// Draw remaining
	if (batch_quads > 0) {
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), buf);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), buf + 2);
		glDrawArrays(GL_QUADS, 0, batch_quads * 4);
		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
	}
	
	glUseProgram(0);
}

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
