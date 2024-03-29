/***************************************************************************

  video.c

  Functions to emulate the video hardware of the machine.

***************************************************************************/

#include "emu.h"
#include "video/resnet.h"
#include "includes/hyperspt.h"

#ifdef KAILLERA
static int no_flip_screen;
#endif /* KAILLERA */

/***************************************************************************

  Convert the color PROMs into a more useable format.

  Hyper Sports has one 32x8 palette PROM and two 256x4 lookup table PROMs
  (one for characters, one for sprites).
  The palette PROM is connected to the RGB output this way:

  bit 7 -- 220 ohm resistor  -- BLUE
        -- 470 ohm resistor  -- BLUE
        -- 220 ohm resistor  -- GREEN
        -- 470 ohm resistor  -- GREEN
        -- 1  kohm resistor  -- GREEN
        -- 220 ohm resistor  -- RED
        -- 470 ohm resistor  -- RED
  bit 0 -- 1  kohm resistor  -- RED

***************************************************************************/

PALETTE_INIT( hyperspt )
{
	static const int resistances_rg[3] = { 1000, 470, 220 };
	static const int resistances_b[2] = { 470, 220 };
	double rweights[3], gweights[3], bweights[2];
	int i;

	/* compute the color output resistor weights */
	compute_resistor_weights(0,	255, -1.0,
			3, &resistances_rg[0], rweights, 1000, 0,
			3, &resistances_rg[0], gweights, 1000, 0,
			2, &resistances_b[0],  bweights, 1000, 0);

	/* allocate the colortable */
	machine->colortable = colortable_alloc(machine, 0x20);

	/* create a lookup table for the palette */
	for (i = 0; i < 0x20; i++)
	{
		int bit0, bit1, bit2;
		int r, g, b;

		/* red component */
		bit0 = (color_prom[i] >> 0) & 0x01;
		bit1 = (color_prom[i] >> 1) & 0x01;
		bit2 = (color_prom[i] >> 2) & 0x01;
		r = combine_3_weights(rweights, bit0, bit1, bit2);

		/* green component */
		bit0 = (color_prom[i] >> 3) & 0x01;
		bit1 = (color_prom[i] >> 4) & 0x01;
		bit2 = (color_prom[i] >> 5) & 0x01;
		g = combine_3_weights(gweights, bit0, bit1, bit2);

		/* blue component */
		bit0 = (color_prom[i] >> 6) & 0x01;
		bit1 = (color_prom[i] >> 7) & 0x01;
		b = combine_2_weights(bweights, bit0, bit1);

		colortable_palette_set_color(machine->colortable, i, MAKE_RGB(r, g, b));
	}

	/* color_prom now points to the beginning of the lookup table */
	color_prom += 0x20;

	/* sprites */
	for (i = 0; i < 0x100; i++)
	{
		UINT8 ctabentry = color_prom[i] & 0x0f;
		colortable_entry_set_value(machine->colortable, i, ctabentry);
	}

	/* characters */
	for (i = 0x100; i < 0x200; i++)
	{
		UINT8 ctabentry = (color_prom[i] & 0x0f) | 0x10;
		colortable_entry_set_value(machine->colortable, i, ctabentry);
	}
}

WRITE8_HANDLER( hyperspt_videoram_w )
{
	hyperspt_state *state = space->machine->driver_data<hyperspt_state>();
	state->videoram[offset] = data;
	tilemap_mark_tile_dirty(state->bg_tilemap, offset);
}

WRITE8_HANDLER( hyperspt_colorram_w )
{
	hyperspt_state *state = space->machine->driver_data<hyperspt_state>();
	state->colorram[offset] = data;
	tilemap_mark_tile_dirty(state->bg_tilemap, offset);
}

WRITE8_HANDLER( hyperspt_flipscreen_w )
{
	if (flip_screen_get(space->machine) != (data & 0x01))
	{
		flip_screen_set(space->machine, data & 0x01);
		tilemap_mark_all_tiles_dirty_all(space->machine);
	}
}

static TILE_GET_INFO( get_bg_tile_info )
{
	hyperspt_state *state = machine->driver_data<hyperspt_state>();
	int code = state->videoram[tile_index] + ((state->colorram[tile_index] & 0x80) << 1) + ((state->colorram[tile_index] & 0x40) << 3);
	int color = state->colorram[tile_index] & 0x0f;
	int flags = ((state->colorram[tile_index] & 0x10) ? TILE_FLIPX : 0) | ((state->colorram[tile_index] & 0x20) ? TILE_FLIPY : 0);

	SET_TILE_INFO(1, code, color, flags);
}

VIDEO_START( hyperspt )
{
	hyperspt_state *state = machine->driver_data<hyperspt_state>();

	state->bg_tilemap = tilemap_create(machine, get_bg_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	tilemap_set_scroll_rows(state->bg_tilemap, 32);

#ifdef KAILLERA
	no_flip_screen = 0;
	if (!strcmp(machine->gamedrv->name, "hyperspt4p") ||
		!strcmp(machine->gamedrv->name, "hpolym84_4p")) no_flip_screen = 1;
#endif /* KAILLERA */
}

static void draw_sprites( running_machine *machine, bitmap_t *bitmap, const rectangle *cliprect )
{
	hyperspt_state *state = machine->driver_data<hyperspt_state>();
	UINT8 *spriteram = state->spriteram;
	int offs;

	for (offs = state->spriteram_size - 4;offs >= 0;offs -= 4)
	{
		int sx = spriteram[offs + 3];
		int sy = 240 - spriteram[offs + 1];
		int code = spriteram[offs + 2] + 8 * (spriteram[offs] & 0x20);
		int color = spriteram[offs] & 0x0f;
		int flipx = ~spriteram[offs] & 0x40;
		int flipy = spriteram[offs] & 0x80;

		if (flip_screen_get(machine))
		{
			sy = 240 - sy;
			flipy = !flipy;
		}

		/* Note that this adjustment must be done AFTER handling flip_screen_get(machine), thus */
		/* proving that this is a hardware related "feature" */

		sy += 1;

		drawgfx_transmask(bitmap,cliprect,
			machine->gfx[0],
			code, color,
			flipx, flipy,
			sx, sy,
			colortable_get_transpen_mask(machine->colortable, machine->gfx[0], color, 0));

		/* redraw with wraparound */

		drawgfx_transmask(bitmap,cliprect,
			machine->gfx[0],
			code, color,
			flipx, flipy,
			sx - 256, sy,
			colortable_get_transpen_mask(machine->colortable, machine->gfx[0], color, 0));
	}
}

#ifdef KAILLERA
static void rotscreen(running_machine *machine, bitmap_t *bitmap, const rectangle *cliprect)
{
	int x,y;
	const int min_x = cliprect->min_x;
	const int min_y = cliprect->min_y;
	const int max_x = cliprect->max_x;
	const int max_y = cliprect->max_y;

#if 0
	if(machine->color_depth == 32)
	{
		for(y=0; y<((max_y-min_y+1)>>1); y++)
		{
			UINT32 *dst = (UINT32 *)BITMAP_ADDR32(bitmap, min_y + y, min_x);
			UINT32 *src  = (UINT32 *)BITMAP_ADDR32(bitmap, max_y - y, min_x);
			for(x=0; x<max_x-min_x+1; x++)
			{
				const UINT32 tm = src[max_x - x];
				src[max_x - x] = dst[x];
				dst[x] = tm;
			}
		}
	} else
#endif
	{
		for(y=0; y<((max_y-min_y+1)>>1); y++)
		{
			UINT16 *dst = (UINT16 *)BITMAP_ADDR16(bitmap, min_y + y, min_x);
			UINT16 *src  = (UINT16 *)BITMAP_ADDR16(bitmap, max_y - y, min_x);
			for(x=0; x<max_x-min_x+1; x++)
			{
				const UINT16 tm = src[max_x - x];
				src[max_x - x] = dst[x];
				dst[x] = tm;
			}
		}
	}
}
#endif /* KAILLERA */

SCREEN_UPDATE( hyperspt )
{
	hyperspt_state *state = screen->machine->driver_data<hyperspt_state>();
	int row;

	for (row = 0; row < 32; row++)
	{
		int scrollx = state->scroll[row * 2] + (state->scroll[(row * 2) + 1] & 0x01) * 256;
		if (flip_screen_get(screen->machine)) scrollx = -scrollx;
		tilemap_set_scrollx(state->bg_tilemap, row, scrollx);
	}

	tilemap_draw(bitmap, cliprect, state->bg_tilemap, 0, 0);
	draw_sprites(screen->machine, bitmap, cliprect);

#ifdef KAILLERA
	if (no_flip_screen && flip_screen_get(screen->machine)) rotscreen(screen->machine, bitmap, cliprect);
#endif /* KAILLERA */
	return 0;
}

/* Road Fighter */
static TILE_GET_INFO( roadf_get_bg_tile_info )
{
	hyperspt_state *state = machine->driver_data<hyperspt_state>();
	int code = state->videoram[tile_index] + ((state->colorram[tile_index] & 0x80) << 1) + ((state->colorram[tile_index] & 0x60) << 4);
	int color = state->colorram[tile_index] & 0x0f;
	int flags = (state->colorram[tile_index] & 0x10) ? TILE_FLIPX : 0;

	SET_TILE_INFO(1, code, color, flags);
}

VIDEO_START( roadf )
{
	hyperspt_state *state = machine->driver_data<hyperspt_state>();

	state->bg_tilemap = tilemap_create(machine, roadf_get_bg_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	tilemap_set_scroll_rows(state->bg_tilemap, 32);
}
