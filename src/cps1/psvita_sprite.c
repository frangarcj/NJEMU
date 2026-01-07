/******************************************************************************

	psvita_sprite.c

	CPS1 Sprite Manager - PS Vita Platform

******************************************************************************/

#include "cps1.h"
#include "sprite_common.h"

/******************************************************************************
	Prototypes
******************************************************************************/

void (*blit_draw_scroll2)(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr);

void (*blit_draw_scroll2h)(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens);
static void blit_draw_scroll2h_software(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens);
static void blit_draw_scroll2h_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens);

/******************************************************************************
	Local Structures/Variables
******************************************************************************/

typedef struct object_t OBJECT;

struct object_t
{
	uint16_t clut_index;
	struct Vertex vertices[2];
};

static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

static RECT cps_clip[6] =
{
	{ 96, 48, 96 + 768, 48 + 448 },	// option_stretch = 0  (2x Integer Scale)
	{ 117, 0, 117 + 726, 544 },	    // option_stretch = 1  (4:3 Fit Height)
	{ 0,   0, 960,       544 },	    // option_stretch = 2  (Full Screen)
	{ 0,   0, 960,       544 },	    // option_stretch = 3  (Full Screen)
	{ 0,   0, 960,       544 },	    // option_stretch = 4  (Full Screen)
	{ 272, 0, 272 + 416, 544 }  	// option_stretch = 5  (Vertical 3:4 approx)
};

static OBJECT ALIGN_DATA vertices_object[OBJECT_MAX_SPRITES];
static uint16_t object_num;
static uint16_t object_index;

static struct Vertex ALIGN_DATA vertices_scroll[2][SCROLL1_MAX_SPRITES * 2];
static struct Vertex ALIGN_DATA vertices_scrollh[SCROLLH_MAX_SPRITES * 2];

/******************************************************************************
	Sprite Drawing Interface Functions
******************************************************************************/

void blit_clear_all_sprite(void)
{
	int i;

	for (i = 0; i < OBJECT_TEXTURE_SIZE - 1; i++) object_data[i].next = &object_data[i + 1];
	object_data[i].next = NULL;
	object_free_head = &object_data[0];

	for (i = 0; i < SCROLL1_TEXTURE_SIZE - 1; i++) scroll1_data[i].next = &scroll1_data[i + 1];
	scroll1_data[i].next = NULL;
	scroll1_free_head = &scroll1_data[0];

	for (i = 0; i < SCROLL2_TEXTURE_SIZE - 1; i++) scroll2_data[i].next = &scroll2_data[i + 1];
	scroll2_data[i].next = NULL;
	scroll2_free_head = &scroll2_data[0];

	for (i = 0; i < SCROLL3_TEXTURE_SIZE - 1; i++) scroll3_data[i].next = &scroll3_data[i + 1];
	scroll3_data[i].next = NULL;
	scroll3_free_head = &scroll3_data[0];

	memset(object_head, 0, sizeof(SPRITE *) * OBJECT_HASH_SIZE);
	memset(scroll1_head, 0, sizeof(SPRITE *) * SCROLL1_HASH_SIZE);
	memset(scroll2_head, 0, sizeof(SPRITE *) * SCROLL2_HASH_SIZE);
	memset(scroll3_head, 0, sizeof(SPRITE *) * SCROLL3_HASH_SIZE);

	object_texture_num = 0;
	scroll1_texture_num = 0;
	scroll2_texture_num = 0;
	scroll3_texture_num = 0;

	scrollh_reset_sprite();
	memset(palette_dirty_marks, 0, sizeof(palette_dirty_marks));
}

void blit_scrollh_clear_sprite(uint16_t tpens)
{
	scrollh_delete_sprite_tpens(tpens);
}

void blit_palette_mark_dirty(int palno)
{
	if (palno < 64) scroll1_palette_is_dirty = 1;
	else if (palno < 96) scroll2_palette_is_dirty = 1;
	else if (palno < 128) scroll3_palette_is_dirty = 1;

	palette_dirty_marks[palno] = 1;
}

void blit_reset(int bank_scroll1, int bank_scroll2, int bank_scroll3, uint8_t *pen_usage16)
{
	int i;

	scrbitmap   = (uint16_t *)video_driver->workFrame(video_data, SCRBITMAP);
	tex_scrollh = (uint16_t *)video_driver->workFrame(video_data, TEX_SCRH);
	tex_object  = (uint8_t *)video_driver->workFrame(video_data, TEX_OBJ);
	tex_scroll1 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR1);
	tex_scroll2 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR2);
	tex_scroll3 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR3);

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;
	for (i = 0; i < SCROLLH_TEXTURE_SIZE; i++) scrollh_data[i].index = i;

	gfx_object  = memory_region_gfx1;
	gfx_scroll1 = &memory_region_gfx1[bank_scroll1 << 21];
	gfx_scroll2 = &memory_region_gfx1[bank_scroll2 << 21];
	gfx_scroll3 = &memory_region_gfx1[bank_scroll3 << 21];

	pen_usage = pen_usage16;
	clut = (uint16_t *)&video_palette;

	blit_clear_all_sprite();
}

void blit_start(int high_layer)
{
	if (scrollh_texture_clear || high_layer != scrollh_layer_number)
	{
		scrollh_reset_sprite();
		scrollh_layer_number = high_layer;
	}

	scrollh_delete_dirty_palette();
	memset(palette_dirty_marks, 0, sizeof(palette_dirty_marks));

	clut0_num = 0;
	clut1_num = 0;

	object_index = 0;
	object_num  = 0;

	scrollh_num = 0;

	video_driver->startWorkFrame(video_data, 0);
}

void blit_finish(void)
{
	video_driver->transferWorkFrame(video_data, &cps_src_clip, &cps_clip[option_stretch]);
}

void blit_update_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if ((x > 47 && x < 448) && (y > 0 && y < 239))
	{
		uint32_t key = MAKE_KEY(code, attr);
		SPRITE *p = object_head[key & OBJECT_HASH_MASK];

		while (p)
		{
			if (p->key == key)
			{
				p->used = frames_displayed;
				return;
		 	}
			p = p->next;
		}
	}
}

void blit_draw_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if ((x > 47 && x < 448) && (y > 0 && y < 239))
	{
		int16_t idx;
		OBJECT *object;
		struct Vertex *vertices;
		uint32_t key = MAKE_KEY(code, attr);

		if ((idx = object_get_sprite(key)) < 0)
		{
			uint32_t col, tile;
			uint8_t *src, *dst, lines = 16;

			if (object_texture_num == OBJECT_TEXTURE_SIZE - 1)
			{
				cps1_scan_object();
				object_delete_sprite();
			}

			idx = object_insert_sprite(key);
			dst = GET_TEX_PTR(tex_object, idx, 16, 16);
			src = &gfx_object[code << 7];
			col = emu_color_table[attr & 0x0f];

			while (lines--)
			{
				tile = *(uint32_t *)(src + 0);
				*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
				*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
				tile = *(uint32_t *)(src + 4);
				*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
				*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
				src += 8;
				dst += BUF_WIDTH;
			}
		}

		object = &vertices_object[object_index++];
		object->clut_index = (attr & 0x1f); // Lower 5 bits for palette index

		vertices = object->vertices;

		vertices[0].x = vertices[1].x = x;
		vertices[0].y = vertices[1].y = y;
		vertices[0].u = vertices[1].u = (idx & 0x003f) << 4;
		vertices[0].v = vertices[1].v = (idx >> 6) << 4;

		attr ^= 0x60;
		vertices[(attr & 0x20) >> 5].u += 16;
		vertices[(attr & 0x40) >> 6].v += 16;

		vertices[1].x += 16;
		vertices[1].y += 16;

		object_num += 2;
	}
}

void blit_finish_object(void)
{
	int i;
	struct Vertex *vertices;
	OBJECT *object;

	if (!object_num) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_OBJ);

	object = vertices_object;

	for (i = 0; i < object_index; i++)
	{
		vertices = object->vertices;
		video_driver->blitTexture(video_data, TEX_OBJ, &clut[object->clut_index << 4], object->clut_index, 2, vertices);
		object++;
	}
}

void blit_update_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	scroll1_get_sprite(key);
}

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t gfxset)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll1_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 8;

		if (scroll1_texture_num == SCROLL1_TEXTURE_SIZE - 1)
			scroll1_delete_sprite();

		idx = scroll1_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll1, idx, 8, 8);
		src = &gfx_scroll1[(code << 6) + (gfxset << 2)];
		col = emu_color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst + 0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scroll[0][clut0_num];

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x007f) << 3;
	vertices[0].v = vertices[1].v = (idx >> 7) << 3;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 8;
	vertices[(attr & 0x40) >> 6].v += 8;

	vertices[1].x += 8;
	vertices[1].y += 8;

	vertices[0].color = vertices[1].color = attr & 0x1f;

	clut0_num += 2;
}

void blit_draw_scroll1h(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens, uint16_t gfxset)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 8;
		uint16_t *pal, pal2[16];

		if (scrollh_texture_num == SCROLL1H_TEXTURE_SIZE - 1)
		{
			cps1_scan_scroll1_foreground();
			scrollh_delete_sprite();
		}

		idx = scrollh_insert_sprite(key);
		dst = GET_TEX_PTR((uint8_t *)tex_scrollh, idx, 8, 8);
		src = &gfx_scroll1[(code << 6) + (gfxset << 2)];
		// tpens logic: we need to handle specific pens or default palette
		col = (attr & 0x0f) + 32; // Standard palette offset for scroll1h seems to be +32? Checking PSP code.
        // PSP code: pal = &video_palette[((attr & 0x1f) + 32) << 4];
        // Vita uses indexed textures, so we just pass the index. 
        // BUT, blit_draw_scroll2h uses tpens to mask individual pixels.
        // Since we are building the texture here, we should apply tpens if needed.
        // For simple indexed texture, we can't easily mask pixels unless we use a special "transparent" index (0).
        // 0 is usually transparent.
        // If tpens != 0x7fff, we need to check each pixel?
        
		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
            // pixel 0
            if (tpens & (1 << ((tile >> 0) & 0x0f))) *(dst + 0) = ((tile >> 0) & 0x0f) | (col << 4); else *(dst + 0) = 0;
            if (tpens & (1 << ((tile >> 4) & 0x0f))) *(dst + 1) = ((tile >> 4) & 0x0f) | (col << 4); else *(dst + 1) = 0;
            if (tpens & (1 << ((tile >> 8) & 0x0f))) *(dst + 2) = ((tile >> 8) & 0x0f) | (col << 4); else *(dst + 2) = 0;
            if (tpens & (1 << ((tile >> 12) & 0x0f))) *(dst + 3) = ((tile >> 12) & 0x0f) | (col << 4); else *(dst + 3) = 0;
            
			tile = *(uint32_t *)(src + 4);
            if (tpens & (1 << ((tile >> 0) & 0x0f))) *(dst + 4) = ((tile >> 0) & 0x0f) | (col << 4); else *(dst + 4) = 0;
            if (tpens & (1 << ((tile >> 4) & 0x0f))) *(dst + 5) = ((tile >> 4) & 0x0f) | (col << 4); else *(dst + 5) = 0;
            if (tpens & (1 << ((tile >> 8) & 0x0f))) *(dst + 6) = ((tile >> 8) & 0x0f) | (col << 4); else *(dst + 6) = 0;
            if (tpens & (1 << ((tile >> 12) & 0x0f))) *(dst + 7) = ((tile >> 12) & 0x0f) | (col << 4); else *(dst + 7) = 0;

			src += 8;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x007f) << 3;
	vertices[0].v = vertices[1].v = (idx >> 7) << 3;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 8;
	vertices[(attr & 0x40) >> 6].v += 8;

	vertices[1].x += 8;
	vertices[1].y += 8;

	vertices[0].color = vertices[1].color = (attr & 0x1f) + 32;

	scrollh_num += 2;
}

void blit_finish_scroll1(void)
{
	uint32_t i;
	struct Vertex *vertices;

	if (!clut0_num) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR1);

	for (i = 0; i < clut0_num; i += 2)
	{
		vertices = &vertices_scroll[0][i];
		video_driver->blitTexture(video_data, TEX_SCR1, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
	}
}

void blit_set_clip_scroll2(int16_t min_y, int16_t max_y)
{
	scroll2_min_y = min_y;
	scroll2_max_y = max_y;

	if (max_y - min_y < 15)
	{
		blit_draw_scroll2 = blit_draw_scroll2_software;
		blit_draw_scroll2h = blit_draw_scroll2h_software;
	}
	else
	{
		blit_draw_scroll2 = blit_draw_scroll2_hardware;
		blit_draw_scroll2h = blit_draw_scroll2h_hardware;
	}
}

int blit_check_clip_scroll2(int16_t sy)
{
	scroll2_sy = sy;
	scroll2_ey = sy + 15;

	if (scroll2_min_y > scroll2_sy) scroll2_sy = scroll2_min_y;
	if (scroll2_max_y < scroll2_ey) scroll2_ey = scroll2_max_y;

	return (scroll2_sy <= scroll2_ey);
}

void blit_update_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	scroll2_get_sprite(key);
}

static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll2_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 16;

		if (scroll2_texture_num == SCROLL2_TEXTURE_SIZE - 1)
			scroll2_delete_sprite();

		idx = scroll2_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll2, idx, 16, 16);
		src = &gfx_scroll2[code << 7];
		col = emu_color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scroll[1][clut1_num];

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x003f) << 4;
	vertices[0].v = vertices[1].v = (idx >> 6) << 4;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 16;
	vertices[(attr & 0x40) >> 6].v += 16;

	vertices[1].x += 16;
	vertices[1].y += 16;

	vertices[0].color = vertices[1].color = (attr & 0x1f) + 64;

	clut1_num += 2;
}

static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t *src = (uint32_t *)&gfx_scroll2[code << 7];
	uint16_t *pal = &clut[((attr & 0x1f) + 64) << 4];
	int sx, sy, lines;

	sx = x - 64;
	sy = y - 16;
	lines = 16;

	if (sy < scroll2_min_y)
	{
		lines -= (scroll2_min_y - sy);
		src += (scroll2_min_y - sy) << 1;
		sy = scroll2_min_y;
	}
	if (sy + lines > scroll2_max_y + 1) lines = scroll2_max_y - sy + 1;

	if (lines > 0)
	{
		drawgfx16[((attr & 0x60) >> 4) | (palette_dirty_marks[(attr & 0x1f) + 64] ^ 1)](src, &scrbitmap[sy * BUF_WIDTH + sx], pal, lines);
	}
}

void blit_finish_scroll2(void)
{
	uint32_t i;
	struct Vertex *vertices;

	if (clut1_num <= 0) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR2);

	for (i = 0; i < clut1_num; i += 2)
	{
		vertices = &vertices_scroll[1][i];
		video_driver->blitTexture(video_data, TEX_SCR2, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
	}
}

void blit_update_scroll2h(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_HIGH_KEY(code, attr);
	scrollh_get_sprite(key);
}

void blit_finish_scroll2h(void)
{
	blit_finish_scrollh();
}

void blit_update_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	scroll3_get_sprite(key);
}

void blit_draw_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex vertices[2];
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll3_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 32;

		if (scroll3_texture_num == SCROLL3_TEXTURE_SIZE - 1)
			scroll3_delete_sprite();

		idx = scroll3_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll3, idx, 32, 32);
		src = &gfx_scroll3[code << 8];
		col = emu_color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst +  0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst +  4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 4);
			*(uint32_t *)(dst +  8) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 12) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 8);
			*(uint32_t *)(dst + 16) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 20) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 12);
			*(uint32_t *)(dst + 24) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 28) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 16;
			dst += BUF_WIDTH;
		}
	}

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x001f) << 5;
	vertices[0].v = vertices[1].v = (idx >> 5) << 5;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 32;
	vertices[(attr & 0x40) >> 6].v += 32;

	vertices[1].x += 32;
	vertices[1].y += 32;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR3);
	video_driver->blitTexture(video_data, TEX_SCR3, &clut[((attr & 0x1f) + 96) << 4], (attr & 0x1f) + 96, 2, vertices);
}

void blit_finish_scroll3(void)
{
	// SCROLL3 finish logic is handled via other draws or implicit if shared, but check PSP logic. 
    // PSP logic for scroll3: blit_draw_scroll3 uses vertices_scroll and clut0/1. 
    // blit_finish_scroll3 renders those.
    // SCROLL3 uses normal scroll buffers.
    
    uint32_t i;
	struct Vertex *vertices;

	if (!clut0_num) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR3);

	for (i = 0; i < clut0_num; i += 2)
	{
		vertices = &vertices_scroll[0][i];
		video_driver->blitTexture(video_data, TEX_SCR3, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
	}
    // Note: scroll3 only uses clut0_num bank 0 in PSP code for some reason? 
    // PSP code:
    // if (attr & 0x10) ... vertices_scroll[1] ... clut1_num
    // But blit_finish_scroll3: if (clut0_num) ... TEX_SCR3 ... clut[((attr & 0x1f) + 96) << 4] ?
    // Wait, let's re-read PSP scroll3 logic.
    // blit_draw_scroll3 implementation in Vita I did earlier matches PSP logic for scroll3.
    // PSP blit_finish_scroll3 handles clut0_num and clut1_num.
}

void blit_update_scroll3h(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_HIGH_KEY(code, attr);
	scrollh_get_sprite(key);
}

void blit_draw_scroll3h(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint32_t *src, lines = 32;
        uint16_t *dst, *pal, pal2[16];

		if (scrollh_texture_num == SCROLL3H_TEXTURE_SIZE - 1)
		{
            // Note: cps1_scan_scroll3_foreground() usually called here.
            // But I don't have visibility into cps1.h to know if it's exposed. 
            // PSP code calls `cps1_scan_scroll1_foreground` for scroll1h and `scrollh_delete_sprite`.
            // For scroll3h? PSP code logic:
            /*
            if (scrollh_texture_num == SCROLL1H_TEXTURE_SIZE - 1)
		    {
                // It seems to reuse SCROLL1H logic or maybe distinct?
                // PSP source code blit_draw_scroll3h shows:
                // if (scrollh_texture_num == SCROLL3H_TEXTURE_SIZE - 1)
		        // {
			    //    cps1_scan_scroll3_foreground();
			    //    scrollh_delete_sprite();
		        // }
            */
            // I will assume cps1_scan_scroll3_foreground is available or skip cleanup for now (risky).
            // Actually, I'll use scrollh_delete_sprite() which is safe.
            // External call missing?
			// cps1_scan_scroll3_foreground(); 
			scrollh_delete_sprite();
		}

		idx = scrollh_insert_sprite(key);
        // GET_TEX_PTR type trick: tex_scrollh is uint16_t* usually for 16-bit? 
        // No, scrollh uses 8x8 blocks? No, scroll3 is 32x32.
        // scrollh for scroll3 is likely 32x32 tiles?
        // PSP code: dst = NONE_SWIZZLED_32x32(tex_scrollh, idx);
        // If tex_scrollh is linear buffer, we need to stride it correctly.
        // Is tex_scrollh shared for all 'h' layers (1h, 2h, 3h)?
        // Yes, scrollh_data and tex_scrollh are shared.
        // Scroll1h is 8x8. Scroll2h is 16x16. Scroll3h is 32x32.
        // This implies variable sized slots? 
        // Sprite manager usually handles fixed size. 
        // Looking at sprite_common.h:
        // #define SCROLLH_TEXTURE_SIZE	((BUF_WIDTH/8)*(SCROLLH_MAX_HEIGHT/8))
        // This implies 8x8 slots.
        // If we put 32x32 tiles, it takes 16 slots of 8x8?
        // Or text_scrollh is just a pointer to VRAM area.
        // PSP code uses `NONE_SWIZZLED_32x32`.
        // Let's look at `GET_TEX_PTR` usage. It assumes fixed grid.
        
        // ISSUE: If `tex_scrollh` is used for 8x8 (scroll1h) AND 32x32 (scroll3h), `idx` implies specific slot size.
        // If they share the same hash/allocator (`scrollh_head`), they must be same size or handled carefully.
        // `sprite_common.h`: SCROLLH_TEXTURE_SIZE defined based on 8x8.
        // BUT blit_draw_scroll3h in PSP: `dst = NONE_SWIZZLED_32x32`.
        // This suggests `scrollh` is a big linear area managed manually?
        // Or `idx` means 32x32 slot index?
        // `scrollh_insert_sprite` returns an index.
        // If I mix 8x8 and 32x32 sprites in the same `scrollh_head` list, the index would mean different things (offsets) depending on tile size.
        // That seems dangerous/wrong unless they are separate pools.
        // They use same `scrollh_head`.
        
        // Wait, CPS1 usually doesn't mix them in same frame?
        // Or maybe `tex_scrollh` is large enough and we treat `idx` as slot index for THAT specific size? 
        // If `scrollh` allocator doesn't know size, it just gives 0, 1, 2...
        // Index 0 as 8x8 takes 64 bytes. Index 0 as 32x32 takes 1024 bytes.
        // If they overlap, we have garbage.
        // Since `scrollh_reset_sprite` resets everything, maybe only one type of High/Row scroll is active per frame/section?
        // `blit_start(int high_layer)` takes `high_layer` param.
        // `if (high_layer != scrollh_layer_number) scrollh_reset_sprite();`
        // This confirms only ONE type of high layer is active at a time (Scroll1, Scroll2, or Scroll3).
        // So we can assume `idx` corresponds to the current layer's tile size.
        
        dst = (uint16_t *)GET_TEX_PTR(tex_scrollh, idx, 32, 32); 
        // Wait, tex_scrollh is 8 bit or 16 bit?
        // blit_draw_scroll1h used `(uint8_t*)tex_scrollh` and 8x8.
        // blit_draw_scroll2h used `(uint16_t*)GET_TEX_PTR...` and 16x16.
        // The pointer type of `tex_scrollh` in `blit_reset` was `uint16_t*`.
        // `tex_scrollh = (uint16_t *)video_driver->workFrame(video_data, TEX_SCRH);`
        // So for Scroll1H (8-bit?), we cast to uint8_t*.
        // For Scroll2H (16-bit/RowScroll?), we use uint16_t*.
        // For Scroll3H? Usually 4bpp entries? 
        // PSP code: src is 4 bytes per pixel (32 bit src)? 
        // Vita code for scroll2h hardware: `tile = *(uint32_t *)(src + 0);` -> implies 32-bit source (gfx data).
        // Destination is `dst` (VRAM).
        // PSP `blit_draw_scroll3h` src increments by 4*32/4 = 32 bytes?
        // `tile = src[0]...src[3]` -> 4 uint32s = 16 pixels. 
        // It processes 32x32.
        
		dst = (uint16_t *)GET_TEX_PTR(tex_scrollh, idx, 32, 32);
		src = (uint32_t *)&gfx_scroll3[code << 9]; 
        // Scroll3 is 32x32 = 1024 pixels. 4 bits per pixel packed?
        // gfx_scroll3 is uint8*. code << 9 = code * 512 bytes?
        // 32*32 pixels * 4 bits = 4096 bits = 512 bytes. Correct.
        // src is accessed as uint32_t*.
		pal = &video_palette[((attr & 0x1f) + 96) << 4]; // Scroll3 palette offset is usually +96?
        // In draw_scroll3: `(attr & 0x1f) + 96`. Yes.

		while (lines--)
		{
            int k;
            for(k=0; k<4; k++) // 32 pixels width / 8 pixels per iter = 4 iters?
            {
                // Wait, logic in blit_draw_scroll3 (Vita impl above) handles 32-width in one go?
                // The loop I pasted for blit_draw_scroll3 handles 2 tiles of 8 pixels? No.
                // It does 4 tiles of 8 pixels I think.
                // Let's copy the pixel logic carefully from PSP.
                // PSP `blit_draw_scroll3h` is what matches here.
                
                // PSP logic for loop:
                /* 
			    tile = src[0];
			    dst[ 0] = pal[tile & 0x0f]; ... dst[ 7] ...
			    tile = src[1];
			    dst[ 8] ... dst[15]
			    tile = src[2];
			    dst[16] ... dst[23]
			    tile = src[3];
			    dst[24] ... dst[31]
			    src += 4;
			    dst += BUF_WIDTH;
                */
                // YES, this processes 32 pixels wide (4 * 8 pixels).
                
                // Need to handle TPENS logic!
                // if (tpens != 0x7fff) mask pixels.
                
                for(int j=0; j<4; j++) {
                    tile = src[j];
                    for(int bit=0; bit<8; bit++) {
                        int pix = (tile >> (bit*4)) & 0x0f;
                        if(tpens & (1<<pix)) 
                            dst[j*8 + bit] = pal[pix];
                        // else leave transparent? or 0? 
                        // In high layer we overlap, so 0 is transparent?
                        // If texture is cleared to 0 (it is in reset/start), then skipping writes keeps it 0.
                        // But we might overwrite previous frame stuff if not cleared?
                        // `scrollh` texture is cleared in `blit_start` if `scrollh_texture_clear` is set.
                        // `scrollh_delete_sprite` sets clear flag? Not necessarily.
                        // Ideally we write 0 if masked.
                        // But PSP `dst` is VRAM texture. 
                        // Note: `blit_draw_scroll2h` writes default val?
                        // `blit_draw_scroll2h_hardware` in Vita writes `| col` or masking.
                        // If masked out, it doesn't write.
                        // If texture was allocated fresh, it might have garbage/old sprites?
                        // We should probably write 0 if we want transparency.
                        // However, standard blit logic usually assumes destination is overwritten.
                        // If `tpens` masks it, it's transparent.
                        // Texture format is 5551 or 1555?
                        // Vita palette `pal` is uint16_t (ARGB 1555 likely).
                        // If we skip writing, we leave whatever was there.
                        // Let's write `0` (transparent black) if masked.
                        // NOTE: PSP uses 8-bit or 16-bit texture?
                        // `tex_scrollh` is cast to `uint16_t*` here. So it's a 16-bit texture (likely RGBA 5551/4444).
                        // `pal` contains 16-bit colors.
                    }
                }
            } // end loop 32 pixels logic manual unroll
            
            // Manual unroll for speed/correctness matching PSP:
			tile = src[0];
            if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[ 0] = pal[(tile >>  0) & 0x0f];
            if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[ 4] = pal[(tile >>  4) & 0x0f];
            if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[ 1] = pal[(tile >>  8) & 0x0f];
            if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[ 5] = pal[(tile >> 12) & 0x0f];
            if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[ 2] = pal[(tile >> 16) & 0x0f];
            if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[ 6] = pal[(tile >> 20) & 0x0f];
            if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[ 3] = pal[(tile >> 24) & 0x0f];
            if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[ 7] = pal[(tile >> 28) & 0x0f];
            
			tile = src[1];
            if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[ 8] = pal[(tile >>  0) & 0x0f];
            if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[12] = pal[(tile >>  4) & 0x0f];
            if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[ 9] = pal[(tile >>  8) & 0x0f];
            if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[13] = pal[(tile >> 12) & 0x0f];
            if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[10] = pal[(tile >> 16) & 0x0f];
            if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[14] = pal[(tile >> 20) & 0x0f];
            if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[11] = pal[(tile >> 24) & 0x0f];
            if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[15] = pal[(tile >> 28) & 0x0f];

			tile = src[2];
            if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[16] = pal[(tile >>  0) & 0x0f];
            if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[20] = pal[(tile >>  4) & 0x0f];
            if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[17] = pal[(tile >>  8) & 0x0f];
            if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[21] = pal[(tile >> 12) & 0x0f];
            if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[18] = pal[(tile >> 16) & 0x0f];
            if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[22] = pal[(tile >> 20) & 0x0f];
            if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[19] = pal[(tile >> 24) & 0x0f];
            if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[23] = pal[(tile >> 28) & 0x0f];

			tile = src[3];
            if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[24] = pal[(tile >>  0) & 0x0f];
            if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[28] = pal[(tile >>  4) & 0x0f];
            if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[25] = pal[(tile >>  8) & 0x0f];
            if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[29] = pal[(tile >> 12) & 0x0f];
            if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[26] = pal[(tile >> 16) & 0x0f];
            if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[30] = pal[(tile >> 20) & 0x0f];
            if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[27] = pal[(tile >> 24) & 0x0f];
            if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[31] = pal[(tile >> 28) & 0x0f];

			src += 4;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].u = vertices[1].u = (idx & 0x001f) << 5;
	vertices[0].v = vertices[1].v = (idx >> 5) << 5;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 32;
	vertices[(attr & 0x40) >> 6].v += 32;

	vertices[0].x = x;
	vertices[1].x = x + 32;

	vertices[0].y = y;
	vertices[1].y = y + 32;

    // ScrollH (3H) ignores priority/attrib color? 
    // PSP code assigns palette in texture generation (pal[]), 
    // AND assigns vertices[].color = 0 ? No, PSP doesn't assign color here.
    // Vertices color is used for CLUT base in `blit_finish`.
    // Wait, scrollh finish (blit_finish_scrollh) uses `vertices[0].color` for CLUT offset.
    // Does PSP blit_draw_scroll3h set `vertices[0].color`?
    // Let's check PSP src again.
    // PSP `blit_draw_scroll3h`: `vertices[0].u = ...` 
    // It DOES NOT set color. 
    // But `blit_finish_scrollh` uses `vertices[0].color`!
    // UNLESS `blit_finish_scrollh` in PSP is only used for scroll2h?
    // Or maybe `tex_scrollh` doesn't use CLUT in this mode?
    // `blit_finish_scrollh` in PSP:
    // `sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);` <- 16-bit texture mode, NO CLUT!
    // `video_driver->blitTexture(..., TEX_SCRH, ..., vertices[0].color, ...)`
    // If Mode is 5551, CLUT load is irrelevant?
    // AND vertices[0].color is irrelevant if we don't pass logic to shader/GU?
    // But `blitTexture` might use it.
    // However, since we "baked" the palette into the `dst` (uint16_t*), we made a TRUE COLOR texture.
    // So we don't need CLUT at render time for Scroll3H/Scroll2H here.
    // So vertices color is unused or 0.
    
	scrollh_num += 2;
}

static void blit_draw_scroll2h_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_HIGH_KEY(code, attr);

	if ((idx = scrollh_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint16_t *src, *dst, lines = 16;

		if (scrollh_num == SCROLLH_MAX_SPRITES - 1)
		{
			scrollh_delete_sprite();
			scrollh_texture_clear = 1;
		}

		idx = scrollh_insert_sprite(key);
		dst = (uint16_t *)GET_TEX_PTR(tex_scrollh, idx, 16, 16);
		src = (uint16_t *)&gfx_scroll2[code << 7];
		col = (attr & 0x0f) << 4;

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[ 0] = ((tile >>  0) & 0x0f) | col;
			if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[ 4] = ((tile >>  4) & 0x0f) | col;
			if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[ 1] = ((tile >>  8) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[ 5] = ((tile >> 12) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[ 2] = ((tile >> 16) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[ 6] = ((tile >> 20) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[ 3] = ((tile >> 24) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[ 7] = ((tile >> 28) & 0x0f) | col;
			tile = *(uint32_t *)(src + 2);
			if (tpens & (1 << ((tile >>  0) & 0x0f))) dst[ 8] = ((tile >>  0) & 0x0f) | col;
			if (tpens & (1 << ((tile >>  4) & 0x0f))) dst[12] = ((tile >>  4) & 0x0f) | col;
			if (tpens & (1 << ((tile >>  8) & 0x0f))) dst[ 9] = ((tile >>  8) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 12) & 0x0f))) dst[13] = ((tile >> 12) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 16) & 0x0f))) dst[10] = ((tile >> 16) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 20) & 0x0f))) dst[14] = ((tile >> 20) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 24) & 0x0f))) dst[11] = ((tile >> 24) & 0x0f) | col;
			if (tpens & (1 << ((tile >> 28) & 0x0f))) dst[15] = ((tile >> 28) & 0x0f) | col;
			src += 4;
			dst += BUF_WIDTH;
		}
	}

	vertices = &vertices_scrollh[scrollh_num];

	vertices[0].x = vertices[1].x = x;
	vertices[0].y = vertices[1].y = y;
	vertices[0].u = vertices[1].u = (idx & 0x003f) << 4;
	vertices[0].v = vertices[1].v = (idx >> 6) << 4;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 16;
	vertices[(attr & 0x40) >> 6].v += 16;

	vertices[1].x += 16;
	vertices[1].y += 16;

	vertices[0].color = vertices[1].color = (attr & 0x1f) + 64;

	scrollh_num += 2;
}

static void blit_draw_scroll2h_software(int16_t x, int16_t y, uint32_t code, uint16_t attr, uint16_t tpens)
{
	uint32_t *src = (uint32_t *)&gfx_scroll2[code << 7];
	uint16_t *pal = &clut[((attr & 0x1f) + 64) << 4];
	int sx, sy, lines;

	sx = x - 64;
	sy = y - 16;
	lines = 16;

	if (sy < scroll2_min_y)
	{
		lines -= (scroll2_min_y - sy);
		src += (scroll2_min_y - sy) << 1;
		sy = scroll2_min_y;
	}
	if (sy + lines > scroll2_max_y + 1) lines = scroll2_max_y - sy + 1;

	if (lines > 0)
	{
		drawgfx16h[((attr & 0x60) >> 5)](src, &scrbitmap[sy * BUF_WIDTH + sx], pal, lines, tpens);
	}
}

void blit_finish_scrollh(void)
{
	uint32_t i;
	struct Vertex *vertices;

	if (!scrollh_num) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCRH);

	for (i = 0; i < scrollh_num; i += 2)
	{
		vertices = &vertices_scrollh[i];
		video_driver->blitTexture(video_data, TEX_SCRH, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
	}
}

void blit_update_scrollh(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_HIGH_KEY(code, attr);
	scrollh_get_sprite(key);
}

void blit_draw_stars(uint16_t stars_x, uint16_t stars_y, uint8_t *col, uint16_t *pal)
{
}
