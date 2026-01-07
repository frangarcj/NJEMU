/******************************************************************************

	sprite.c

	CPS2 Sprite Manager - PSP Platform

******************************************************************************/

#include "cps2.h"
#include "sprite_common.h"

/******************************************************************************
	Macros
******************************************************************************/

#define PSP_UNCACHE_PTR(p)              (((uint32_t)(p)) | 0x40000000)

/******************************************************************************
	Prototypes
******************************************************************************/

void (*blit_finish_object)(int start_pri, int end_pri);
static void blit_render_object(int start_pri, int end_pri);
static void blit_render_object_zb(int start_pri, int end_pri);

void (*blit_draw_scroll2)(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_software(int16_t x, int16_t y, uint32_t code, uint16_t attr);
static void blit_draw_scroll2_hardware(int16_t x, int16_t y, uint32_t code, uint16_t attr);

/******************************************************************************
	Shared with sprite_common
******************************************************************************/

static RECT cps_src_clip = { 64, 16, 64 + 384, 16 + 224 };

static RECT cps_clip[6] =
{
	{ 48, 24, 48 + 384, 24 + 224 },
	{ 60,  1, 60 + 360,  1 + 270 },
	{ 48,  1, 48 + 384,  1 + 270 },
	{ 7,   0,   7+ 466,      272 },
	{ 0,   1, 480,       1 + 270 },
	{ 138, 0, 138 + 204,     272 }
};

static const int ALIGN_DATA swizzle_table_8bit[32] =
{
	   0, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16,
	3984, 16, 16, 16, 16, 16, 16, 16
};

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
}

void blit_reset(void)
{
	int i;

	scrbitmap   = (uint16_t *)video_driver->workFrame(video_data, SCRBITMAP);
	tex_object  = video_driver->workFrame(video_data, TEX_SPR0);
	tex_scroll1 = video_driver->workFrame(video_data, TEX_SPR1);
	tex_scroll2 = video_driver->workFrame(video_data, TEX_SPR2);
	tex_scroll3 = video_driver->workFrame(video_data, TEX_FIX);

	for (i = 0; i < OBJECT_TEXTURE_SIZE; i++) object_data[i].index = i;
	for (i = 0; i < SCROLL1_TEXTURE_SIZE; i++) scroll1_data[i].index = i;
	for (i = 0; i < SCROLL2_TEXTURE_SIZE; i++) scroll2_data[i].index = i;
	for (i = 0; i < SCROLL3_TEXTURE_SIZE; i++) scroll3_data[i].index = i;

	clip_min_y = FIRST_VISIBLE_LINE;
	clip_max_y = LAST_VISIBLE_LINE;

	pen_usage = gfx_pen_usage[TILE16];
	clut = (uint16_t *)PSP_UNCACHE_PTR(&video_palette);

	blit_finish_object = blit_render_object;

	blit_clear_all_sprite();
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

	if (cps2_has_mask)
	{
		sceGuStart(GU_DIRECT, gulist);
		sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
		sceGuDisable(GU_ALPHA_TEST);
		sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
		sceGuTexMode(GU_PSM_T8, 0, 0, GU_TRUE);
		sceGuTexFilter(GU_NEAREST, GU_NEAREST);
		sceGuFinish();
		sceGuSync(0, GU_SYNC_FINISH);
	}
}

void blit_finish(void)
{
	if (cps2_has_mask) video_driver->clearFrame(video_data, draw_frame);

	if (cps_rotate_screen)
	{
		if (cps_flip_screen)
		{
			video_driver->copyRectFlip(video_data, work_frame, draw_frame, &cps_src_clip, &cps_src_clip);
			video_driver->copyRect(video_data, draw_frame, work_frame, &cps_src_clip, &cps_src_clip);
			video_driver->clearFrame(video_data, draw_frame);
		}
		video_driver->copyRectRotate(video_data, work_frame, draw_frame, &cps_src_clip, &cps_clip[5]);
	}
	else
	{
		if (cps_flip_screen)
			video_driver->copyRectFlip(video_data, work_frame, draw_frame, &cps_src_clip, &cps_clip[option_stretch]);
		else
			video_driver->transferWorkFrame(video_data, &cps_src_clip, &cps_clip[option_stretch]);
	}
}

void blit_update_object(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	if ((x > 48 && x < 448) && (y > object_min_y && y < clip_max_y))
	{
		uint32_t key = MAKE_KEY(code, attr);
		object_get_sprite(key); // Just to update 'used'
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
			dst = SWIZZLED8_16x16(tex_object, idx);
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
				dst += swizzle_table_8bit[lines];
			}
		}

		object = &vertices_object[object_index++];
		object->clut = attr & 0x10;
		object->next = NULL;

		if (!vertices_object_head[pri])
			vertices_object_head[pri] = object;
		else
			vertices_object_tail[pri]->next = object;

		vertices_object_tail[pri] = object;

		vertices = object->vertices;

		vertices[0].x = vertices[1].x = x;
		vertices[0].y = vertices[1].y = y;
		vertices[0].z = vertices[1].z = z;
		vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
		vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

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
	int i, size = 0, total_sprites = 0;
	uint8_t color = 0;
	struct Vertex *vertices, *vertices_tmp;
	OBJECT *object;

	for (i = start_pri; i <= end_pri; i++)
		size += object_num[i];

	if (!size) return;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(64, clip_min_y, 448, clip_max_y);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_object);
	sceGuClutLoad(256/8, clut);

	vertices_tmp = vertices = (struct Vertex *)sceGuGetMemory(size * sizeof(struct Vertex));

	for (i = start_pri; i <= end_pri; i++)
	{
		object = vertices_object_head[i];

		while (object)
		{
			if (color != object->clut)
			{
				color = object->clut;

				if (total_sprites)
				{
					sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);
					total_sprites = 0;
					vertices = vertices_tmp;
				}

				sceGuClutLoad(256/8, &clut[color << 4]);
			}

			vertices_tmp[0] = object->vertices[0];
			vertices_tmp[1] = object->vertices[1];

			total_sprites += 2;
			vertices_tmp += 2;
			object = object->next;
		}
	}

	if (total_sprites)
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, total_sprites, NULL, vertices);

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

void blit_update_scroll1(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	scroll1_get_sprite(key);
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
		dst = SWIZZLED8_8x8(tex_scroll1, idx);
		src = &memory_region_gfx1[code << 6];
		col = emu_color_table[attr & 0x0f];

		while (lines--)
		{
			tile = *(uint32_t *)(src + 0);
			*(uint32_t *)(dst + 0) = ((tile >> 0) & 0x0f0f0f0f) | col;
			*(uint32_t *)(dst + 4) = ((tile >> 4) & 0x0f0f0f0f) | col;
			src += 8;
			dst += swizzle_table_8bit[lines + 24];
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
	vertices[0].u = vertices[1].u = (idx & 0x003f) << 3;
	vertices[0].v = vertices[1].v = (idx & 0x0fc0) >> 3;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 8;
	vertices[(attr & 0x40) >> 6].v += 8;

	vertices[1].x += 8;
	vertices[1].y += 8;
}

void blit_finish_scroll1(void)
{
	struct Vertex *vertices;

	if (clut0_num + clut1_num == 0) return;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(64, clip_min_y, 448, clip_max_y);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_scroll1);

	vertices = (struct Vertex *)sceGuGetMemory((clut0_num + clut1_num) * sizeof(struct Vertex));

	if (clut0_num)
	{
		sceGuClutLoad(256/8, &clut[32 << 4]);

		memcpy(vertices, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut0_num, NULL, vertices);
		vertices += clut0_num;

		clut0_num = 0;
	}
	if (clut1_num)
	{
		sceGuClutLoad(256/8, &clut[48 << 4]);

		memcpy(vertices, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut1_num, NULL, vertices);

		clut1_num = 0;
	}

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

void blit_set_clip_scroll2(int16_t min_y, int16_t max_y)
{
	scroll2_min_y = min_y;
	scroll2_max_y = max_y + 1;

	if (scroll2_max_y - scroll2_min_y >= 16)
		blit_draw_scroll2 = blit_draw_scroll2_hardware;
	else
		blit_draw_scroll2 = blit_draw_scroll2_software;
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
		{
			if (cps2_scan_scroll2_callback) (*cps2_scan_scroll2_callback)();
			scroll2_delete_sprite();
		}

		idx = scroll2_insert_sprite(key);
		dst = SWIZZLED8_16x16(tex_scroll2, idx);
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
			dst += swizzle_table_8bit[lines];
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
	vertices[0].u = vertices[1].u = (idx & 0x001f) << 4;
	vertices[0].v = vertices[1].v = (idx & 0x03e0) >> 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 16;
	vertices[(attr & 0x40) >> 6].v += 16;

	vertices[1].x += 16;
	vertices[1].y += 16;
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
	struct Vertex *vertices;

	if (clut0_num + clut1_num == 0) return;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(64, scroll2_min_y, 448, scroll2_max_y);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_scroll2);

	vertices = (struct Vertex *)sceGuGetMemory((clut0_num + clut1_num) * sizeof(struct Vertex));

	if (clut0_num)
	{
		sceGuClutLoad(256/8, &clut[64 << 4]);

		memcpy(vertices, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut0_num, NULL, vertices);
		vertices += clut0_num;

		clut0_num = 0;
	}
	if (clut1_num)
	{
		sceGuClutLoad(256/8, &clut[80 << 4]);

		memcpy(vertices, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut1_num, NULL, vertices);

		clut1_num = 0;
	}

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}

void blit_update_scroll3(int16_t x, int16_t y, uint32_t code, uint16_t attr)
{
	uint32_t key = MAKE_KEY(code, attr);
	scroll3_get_sprite(key);
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
		dst = SWIZZLED8_32x32(tex_scroll3, idx);
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
			dst += swizzle_table_8bit[lines];
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
	vertices[0].u = vertices[1].u = (idx & 0x000f) << 5;
	vertices[0].v = vertices[1].v = (idx & 0x00f0) << 1;

	attr ^= 0x60;
	vertices[(attr & 0x20) >> 5].u += 32;
	vertices[(attr & 0x40) >> 6].v += 32;

	vertices[1].x += 32;
	vertices[1].y += 32;
}

void blit_finish_scroll3(void)
{
	struct Vertex *vertices;

	if (clut0_num + clut1_num == 0) return;

	sceGuStart(GU_DIRECT, gulist);
	sceGuDrawBufferList(GU_PSM_5551, work_frame, BUF_WIDTH);
	sceGuScissor(64, clip_min_y, 448, clip_max_y);
	sceGuTexImage(0, 512, 512, BUF_WIDTH, tex_scroll3);

	vertices = (struct Vertex *)sceGuGetMemory((clut0_num + clut1_num) * sizeof(struct Vertex));

	if (clut0_num)
	{
		sceGuClutLoad(256/8, &clut[96 << 4]);

		memcpy(vertices, vertices_scroll[0], clut0_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut0_num, NULL, vertices);
		vertices += clut0_num;

		clut0_num = 0;
	}
	if (clut1_num)
	{
		sceGuClutLoad(256/8, &clut[112 << 4]);

		memcpy(vertices, vertices_scroll[1], clut1_num * sizeof(struct Vertex));
		sceGuDrawArray(GU_SPRITES, TEXTURE_FLAGS, clut1_num, NULL, vertices);

		clut1_num = 0;
	}

	sceGuFinish();
	sceGuSync(0, GU_SYNC_FINISH);
}
