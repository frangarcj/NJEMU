/******************************************************************************

	sprite_common.c

	CPS2 Common sprite management implementation

******************************************************************************/

#include "sprite_common.h"

/******************************************************************************
	Global Variables
******************************************************************************/

int16_t clip_min_y;
int16_t clip_max_y;
int16_t object_min_y;
int16_t scroll2_min_y;
int16_t scroll2_max_y;
int16_t scroll2_sy;
int16_t scroll2_ey;

uint8_t *pen_usage;
uint16_t *scrbitmap;

/* OBJECT */
SPRITE ALIGN_DATA *object_head[OBJECT_HASH_SIZE];
SPRITE ALIGN_DATA object_data[OBJECT_TEXTURE_SIZE];
SPRITE ALIGN_DATA *object_free_head;
uint8_t *tex_object;
uint16_t object_texture_num;

/* SCROLL1 */
SPRITE ALIGN_DATA *scroll1_head[SCROLL1_HASH_SIZE];
SPRITE ALIGN_DATA scroll1_data[SCROLL1_TEXTURE_SIZE];
SPRITE ALIGN_DATA *scroll1_free_head;
uint8_t *tex_scroll1;
uint16_t scroll1_texture_num;

/* SCROLL2 */
SPRITE ALIGN_DATA *scroll2_head[SCROLL2_HASH_SIZE];
SPRITE ALIGN_DATA scroll2_data[SCROLL2_TEXTURE_SIZE];
SPRITE ALIGN_DATA *scroll2_free_head;
uint8_t *tex_scroll2;
uint16_t scroll2_texture_num;

/* SCROLL3 */
SPRITE ALIGN_DATA *scroll3_head[SCROLL3_HASH_SIZE];
SPRITE ALIGN_DATA scroll3_data[SCROLL3_TEXTURE_SIZE];
SPRITE ALIGN_DATA *scroll3_free_head;
uint8_t *tex_scroll3;
uint16_t scroll3_texture_num;

/* Render Data */
OBJECT *vertices_object_head[8];
OBJECT *vertices_object_tail[8];
OBJECT ALIGN_DATA vertices_object[OBJECT_MAX_SPRITES];
uint16_t object_num[8];
uint16_t object_index;
struct Vertex ALIGN_DATA vertices_scroll[2][SCROLL1_MAX_SPRITES * 2];

/* CLUT */
uint16_t *clut;
uint16_t clut0_num;
uint16_t clut1_num;
const uint32_t ALIGN_DATA emu_color_table[16] =
{
	0x00000000, 0x10101010, 0x20202020, 0x30303030,
	0x40404040, 0x50505050, 0x60606060, 0x70707070,
	0x80808080, 0x90909090, 0xa0a0a0a0, 0xb0b0b0b0,
	0xc0c0c0c0, 0xd0d0d0d0, 0xe0e0e0e0, 0xf0f0f0f0
};

/******************************************************************************
	OBJECT Management
******************************************************************************/

int32_t object_get_sprite(uint32_t key)
{
	SPRITE *p = object_head[key & OBJECT_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			if (p->used != frames_displayed)
			{
#if USE_CACHE
				if (update_cache) update_cache(key << 7);
#endif
				p->used = frames_displayed;
			}
			return p->index;
		}
		p = p->next;
	}
	return -1;
}

int32_t object_insert_sprite(uint32_t key)
{
	uint16_t hash = key & OBJECT_HASH_MASK;
	SPRITE *p = object_head[hash];
	SPRITE *q = object_free_head;

	if (!q) return -1;

	object_free_head = object_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		object_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	object_texture_num++;

	return q->index;
}

void object_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < OBJECT_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = object_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				object_texture_num--;

				if (!prev_p)
				{
					object_head[i] = p->next;
					p->next = object_free_head;
					object_free_head = p;
					p = object_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = object_free_head;
					object_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}

/******************************************************************************
	SCROLL1 Management
******************************************************************************/

int32_t scroll1_get_sprite(uint32_t key)
{
	SPRITE *p = scroll1_head[key & SCROLL1_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			if (p->used != frames_displayed)
			{
#if USE_CACHE
				if (update_cache) update_cache(key << 6);
#endif
				p->used = frames_displayed;
			}
			return p->index;
		}
		p = p->next;
	}
	return -1;
}

int32_t scroll1_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL1_HASH_MASK;
	SPRITE *p = scroll1_head[hash];
	SPRITE *q = scroll1_free_head;

	if (!q) return -1;

	scroll1_free_head = scroll1_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll1_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll1_texture_num++;

	return q->index;
}

void scroll1_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL1_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll1_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll1_texture_num--;

				if (!prev_p)
				{
					scroll1_head[i] = p->next;
					p->next = scroll1_free_head;
					scroll1_free_head = p;
					p = scroll1_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll1_free_head;
					scroll1_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}

/******************************************************************************
	SCROLL2 Management
******************************************************************************/

int32_t scroll2_get_sprite(uint32_t key)
{
	SPRITE *p = scroll2_head[key & SCROLL2_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			if (p->used != frames_displayed)
			{
#if USE_CACHE
				if (update_cache) update_cache(key << 7);
#endif
				p->used = frames_displayed;
			}
			return p->index;
		}
		p = p->next;
	}
	return -1;
}

int32_t scroll2_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL2_HASH_MASK;
	SPRITE *p = scroll2_head[hash];
	SPRITE *q = scroll2_free_head;

	if (!q) return -1;

	scroll2_free_head = scroll2_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll2_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll2_texture_num++;

	return q->index;
}

void scroll2_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL2_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll2_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll2_texture_num--;

				if (!prev_p)
				{
					scroll2_head[i] = p->next;
					p->next = scroll2_free_head;
					scroll2_free_head = p;
					p = scroll2_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll2_free_head;
					scroll2_free_head = p;
					p = prev_p->next;
				}
			}
			else
			{
				prev_p = p;
				p = p->next;
			}
		}
	}
}

/******************************************************************************
	SCROLL3 Management
******************************************************************************/

int32_t scroll3_get_sprite(uint32_t key)
{
	SPRITE *p = scroll3_head[key & SCROLL3_HASH_MASK];

	while (p)
	{
		if (p->key == key)
		{
			if (p->used != frames_displayed)
			{
#if USE_CACHE
				if (update_cache) update_cache(key << 9);
#endif
				p->used = frames_displayed;
			}
			return p->index;
		}
		p = p->next;
	}
	return -1;
}

int32_t scroll3_insert_sprite(uint32_t key)
{
	uint16_t hash = key & SCROLL3_HASH_MASK;
	SPRITE *p = scroll3_head[hash];
	SPRITE *q = scroll3_free_head;

	if (!q) return -1;

	scroll3_free_head = scroll3_free_head->next;

	q->next = NULL;
	q->key  = key;
	q->used = frames_displayed;

	if (!p)
	{
		scroll3_head[hash] = q;
	}
	else
	{
		while (p->next) p = p->next;
		p->next = q;
	}

	scroll3_texture_num++;

	return q->index;
}

void scroll3_delete_sprite(void)
{
	int i;
	SPRITE *p, *prev_p;

	for (i = 0; i < SCROLL3_HASH_SIZE; i++)
	{
		prev_p = NULL;
		p = scroll3_head[i];

		while (p)
		{
			if (frames_displayed != p->used)
			{
				scroll3_texture_num--;

				if (!prev_p)
				{
					scroll3_head[i] = p->next;
					p->next = scroll3_free_head;
					scroll3_free_head = p;
					p = scroll3_head[i];
				}
				else
				{
					prev_p->next = p->next;
					p->next = scroll3_free_head;
					scroll3_free_head = p;
					p = prev_p->next;
				}
			}
		}
	}
}

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

/******************************************************************************
	Software Rendering functions
******************************************************************************/

void drawgfx16_16x16(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 2] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 6] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 3] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 7] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[10] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[14] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[11] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[15] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipx(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[13] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 9] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[12] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 8] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 5] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 1] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 4] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 0] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 0] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 4] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 1] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 5] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 2] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 6] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 3] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 7] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 8] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[12] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 9] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[13] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[10] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[14] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[11] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[15] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipxy(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile, mask;

	while (lines--)
	{
		tile = src[0];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[15] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[11] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[14] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[10] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[13] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 9] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[12] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 8] = pal[(tile >> 28) & 0x0f];
		}
		tile = src[1];
		mask = ~tile;
		if (mask)
		{
			if (mask & 0x000f) dst[ 7] = pal[(tile >>  0) & 0x0f];
			if (mask & 0x00f0) dst[ 3] = pal[(tile >>  4) & 0x0f];
			if (mask & 0x0f00) dst[ 6] = pal[(tile >>  8) & 0x0f];
			if (mask & 0xf000) dst[ 2] = pal[(tile >> 12) & 0x0f];
			mask >>= 16;
			if (mask & 0x000f) dst[ 5] = pal[(tile >> 16) & 0x0f];
			if (mask & 0x00f0) dst[ 1] = pal[(tile >> 20) & 0x0f];
			if (mask & 0x0f00) dst[ 4] = pal[(tile >> 24) & 0x0f];
			if (mask & 0xf000) dst[ 0] = pal[(tile >> 28) & 0x0f];
		}
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16_16x16_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 7] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 8] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[15] = pal[tile & 0x0f];
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipx_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[15] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 8] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 7] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 0] = pal[tile & 0x0f];
		src += 2;
		dst += BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[ 0] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 7] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 8] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[15] = pal[tile & 0x0f];
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void drawgfx16_16x16_flipxy_opaque(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines)
{
	uint32_t tile;

	while (lines--)
	{
		tile = src[0];
		dst[15] = pal[tile & 0x0f]; tile >>= 4;
		dst[11] = pal[tile & 0x0f]; tile >>= 4;
		dst[14] = pal[tile & 0x0f]; tile >>= 4;
		dst[10] = pal[tile & 0x0f]; tile >>= 4;
		dst[13] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 9] = pal[tile & 0x0f]; tile >>= 4;
		dst[12] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 8] = pal[tile & 0x0f];
		tile = src[1];
		dst[ 7] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 3] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 6] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 2] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 5] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 1] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 4] = pal[tile & 0x0f]; tile >>= 4;
		dst[ 0] = pal[tile & 0x0f];
		src += 2;
		dst -= BUF_WIDTH;
	}
}

void ALIGN_DATA (*drawgfx16[8])(uint32_t *src, uint16_t *dst, uint16_t *pal, int lines) =
{
	drawgfx16_16x16,
	drawgfx16_16x16_opaque,
	drawgfx16_16x16_flipx,
	drawgfx16_16x16_flipx_opaque,
	drawgfx16_16x16_flipy,
	drawgfx16_16x16_flipy_opaque,
	drawgfx16_16x16_flipxy,
	drawgfx16_16x16_flipxy_opaque
};
