/******************************************************************************

	psvita_sprite.c

	CPS2 Sprite Manager - PS Vita Platform

******************************************************************************/

#include "cps2.h"
#include "sprite_common.h"

/******************************************************************************
	Prototypes
******************************************************************************/

void (*blit_draw_scroll2)(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr);

void (*blit_finish_object)(int start_pri, int end_pri);
static void blit_render_object(int start_pri, int end_pri);

/******************************************************************************
	Local Structures/Variables
******************************************************************************/

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

/******************************************************************************
	Sprite Drawing Interface Functions
******************************************************************************/

void blit_reset(void)
{
	int i;

	scrbitmap   = (uint16_t *)video_driver->workFrame(video_data, SCRBITMAP);
	tex_object  = (uint8_t *)video_driver->workFrame(video_data, TEX_OBJ);
	tex_scroll1 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR1);
	tex_scroll2 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR2);
	tex_scroll3 = (uint8_t *)video_driver->workFrame(video_data, TEX_SCR3);

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	blit_clear_all_sprite();

	blit_finish_object = blit_render_object;
	clut = (uint16_t *)&video_palette;
}

void blit_start(int start, int end)
{
	int i;

	object_index = 0;
	for (i = 0; i < 8; i++)
	{
		object_num[i] = 0;
		vertices_object_head[i] = NULL;
		vertices_object_tail[i] = NULL;
	}

	clut0_num = 0;
	clut1_num = 0;

	object_min_y = start;

	video_driver->startWorkFrame(video_data, 0);
}

void blit_finish(void)
{
	video_driver->transferWorkFrame(video_data, &cps_src_clip, &cps_clip[option_stretch]);
}

void blit_update_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if ((x > 48 && x < 448) && (y > object_min_y && y < clip_max_y))
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

void blit_draw_object(int16_t x, int16_t y, uint16_t z, int16_t pri, uint32_t code, uint16_t attr)
{
	if ((x > 48 && x < 448) && (y > object_min_y && y < clip_max_y))
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
				if (cps2_scan_object_callback) (*cps2_scan_object_callback)();
				object_delete_sprite();
			}

			idx = object_insert_sprite(key);
			dst = GET_TEX_PTR(tex_object, idx, 16, 16);
			src = &memory_region_gfx1[code << 7];
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
		object->clut = attr & 0x1f;
		object->next = NULL;

		if (!vertices_object_head[pri])
			vertices_object_head[pri] = object;
		else
			vertices_object_tail[pri]->next = object;

		vertices_object_tail[pri] = object;

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

		object_num[pri] += 2;
	}
}

static void blit_render_object(int start_pri, int end_pri)
{
	int i;
	OBJECT *object;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_OBJ);

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];
		while (object)
		{
			video_driver->blitTexture(video_data, TEX_OBJ, &clut[object->clut << 4], object->clut, 2, object->vertices);
			object = object->next;
		}
	}
}

void blit_update_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	SPRITE *p = scroll1_head[key & SCROLL1_HASH_MASK];

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

void blit_draw_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll1_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 8;

		if (scroll1_texture_num == SCROLL1_TEXTURE_SIZE - 1)
		{
			if (cps2_scan_scroll1_callback) (*cps2_scan_scroll1_callback)();
			scroll1_delete_sprite();
		}

		idx = scroll1_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll1, idx, 8, 8);
		src = &memory_region_gfx1[code << 6];
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
	scroll2_max_y = max_y + 1;

	if (scroll2_max_y - scroll2_min_y >= 16)
	{
		blit_draw_scroll2 = blit_draw_scroll2_hardware;
	}
	else
	{
		blit_draw_scroll2 = blit_draw_scroll2_software;
	}
}

int blit_check_clip_scroll2(int16_t sy)
{
	scroll2_sy = sy;
	scroll2_ey = sy + 16;

	if (scroll2_min_y > scroll2_sy) scroll2_sy = scroll2_min_y;
	if (scroll2_max_y < scroll2_ey) scroll2_ey = scroll2_max_y;

	return (scroll2_sy < scroll2_ey);
}

void blit_update_scroll2(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if (y + 16 > 0 && y < 239)
	{
		uint32_t key = MAKE_KEY(code, attr);
		SPRITE *p = scroll2_head[key & SCROLL2_HASH_MASK];

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
		{
			if (cps2_scan_scroll2_callback) (*cps2_scan_scroll2_callback)();
			scroll2_delete_sprite();
		}

		idx = scroll2_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll2, idx, 16, 16);
		src = &memory_region_gfx1[code << 7];
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

	vertices[0].color = vertices[1].color = attr & 0x1f;

	clut1_num += 2;
}

static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t *src = (uint32_t *)&memory_region_gfx1[code << 7];
	uint16_t *pal = &clut[(attr & 0x1f) << 4];
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
	if (sy + lines > scroll2_max_y) lines = scroll2_max_y - sy;

	if (lines > 0)
	{
		drawgfx16[((attr & 0x60) >> 4)](src, &scrbitmap[sy * BUF_WIDTH + sx], pal, lines);
	}
}

void blit_finish_scroll2(void)
{
	uint32_t i;
	struct Vertex *vertices;

	if (!clut1_num) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR2);

	for (i = 0; i < clut1_num; i += 2)
	{
		vertices = &vertices_scroll[1][i];
		video_driver->blitTexture(video_data, TEX_SCR2, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
	}
}

void blit_update_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if (y + 32 > 0 && y < 239)
	{
		uint32_t key = MAKE_KEY(code, attr);
		SPRITE *p = scroll3_head[key & SCROLL3_HASH_MASK];

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

void blit_draw_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	int16_t idx;
	struct Vertex *vertices;
	uint32_t key = MAKE_KEY(code, attr);

	if ((idx = scroll3_get_sprite(key)) < 0)
	{
		uint32_t col, tile;
		uint8_t *src, *dst, lines = 32;

		if (scroll3_texture_num == SCROLL3_TEXTURE_SIZE - 1)
		{
			if (cps2_scan_scroll3_callback) (*cps2_scan_scroll3_callback)();
			scroll3_delete_sprite();
		}

		idx = scroll3_insert_sprite(key);
		dst = GET_TEX_PTR(tex_scroll3, idx, 32, 32);
		src = &memory_region_gfx1[code << 9];
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
			*(uint32_t *)(dst + 128) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 132) = ((tile >> 4) & 0x0f0f0f0f) | col;
			tile = *(uint32_t *)(src + 12);
			*(uint32_t *)(dst + 136) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 140) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 16;
			dst += BUF_WIDTH;
		}
	}

	if (attr & 0x10)
	{
		vertices = &vertices_scroll[1][clut1_num];
		clut1_num += 2;
	}
	else
	{
		vertices = &vertices_scroll[0][clut0_num];
		clut0_num += 2;
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

	vertices[0].color = vertices[1].color = attr & 0x1f;
}

void blit_finish_scroll3(void)
{
	uint32_t i;
	struct Vertex *vertices;

	if (clut0_num + clut1_num == 0) return;

	video_driver->setClutBaseAddr(video_data, clut);
	video_driver->uploadMem(video_data, TEX_SCR3);

	if (clut0_num)
	{
		for (i = 0; i < clut0_num; i += 2)
		{
			vertices = &vertices_scroll[0][i];
			video_driver->blitTexture(video_data, TEX_SCR3, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
		}
		clut0_num = 0;
	}

	if (clut1_num)
	{
		for (i = 0; i < clut1_num; i += 2)
		{
			vertices = &vertices_scroll[1][i];
			video_driver->blitTexture(video_data, TEX_SCR3, &clut[vertices[0].color << 4], vertices[0].color, 2, vertices);
		}
		clut1_num = 0;
	}
}
