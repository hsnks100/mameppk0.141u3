/***************************************************************************

  gb.c

  Video file to handle emulation of the Nintendo Game Boy.

  Original code                               Carsten Sorensen   1998
  Mess modifications, bug fixes and speedups  Hans de Goede      1998
  Bug fixes, SGB and GBC code                 Anthony Kruize     2002
  Improvements to match real hardware         Wilbert Pol        2006-2008

  Timing is not accurate enough:
  - Mode 3 takes 172 cycles (measuered with logic analyzer by costis)

***************************************************************************/

#include "emu.h"
#include "cpu/lr35902/lr35902.h"
#include "profiler.h"
#include "includes/gb.h"

#define LCDCONT		state->lcd.gb_vid_regs[0x00]	/* LCD control register                       */
#define LCDSTAT		state->lcd.gb_vid_regs[0x01]	/* LCD status register                        */
#define SCROLLY		state->lcd.gb_vid_regs[0x02]	/* Starting Y position of the background      */
#define SCROLLX		state->lcd.gb_vid_regs[0x03]	/* Starting X position of the background      */
#define CURLINE		state->lcd.gb_vid_regs[0x04]	/* Current screen line being scanned          */
#define CMPLINE		state->lcd.gb_vid_regs[0x05]	/* Gen. int. when scan reaches this line      */
#define BGRDPAL		state->lcd.gb_vid_regs[0x07]	/* Background palette                         */
#define SPR0PAL		state->lcd.gb_vid_regs[0x08]	/* Sprite palette #0                          */
#define SPR1PAL		state->lcd.gb_vid_regs[0x09]	/* Sprite palette #1                          */
#define WNDPOSY		state->lcd.gb_vid_regs[0x0A]	/* Window Y position                          */
#define WNDPOSX		state->lcd.gb_vid_regs[0x0B]	/* Window X position                          */
#define KEY1		state->lcd.gb_vid_regs[0x0D]	/* Prepare speed switch                       */
#define HDMA1		state->lcd.gb_vid_regs[0x11]	/* HDMA source high byte                      */
#define HDMA2		state->lcd.gb_vid_regs[0x12]	/* HDMA source low byte                       */
#define HDMA3		state->lcd.gb_vid_regs[0x13]	/* HDMA destination high byte                 */
#define HDMA4		state->lcd.gb_vid_regs[0x14]	/* HDMA destination low byte                  */
#define HDMA5		state->lcd.gb_vid_regs[0x15]	/* HDMA length/mode/start                     */
#define GBCBCPS		state->lcd.gb_vid_regs[0x28]	/* Backgound palette spec                     */
#define GBCBCPD		state->lcd.gb_vid_regs[0x29]	/* Backgound palette data                     */
#define GBCOCPS		state->lcd.gb_vid_regs[0x2A]	/* Object palette spec                        */
#define GBCOCPD		state->lcd.gb_vid_regs[0x2B]	/* Object palette data                        */

enum {
	UNLOCKED=0,
	LOCKED
};


/* Prototypes */
static TIMER_CALLBACK(gb_lcd_timer_proc);
static TIMER_CALLBACK(gbc_lcd_timer_proc);
static void gb_lcd_switch_on( running_machine *machine );

static const unsigned char palette[] =
{
/* Simple black and white palette */
/*  0xFF,0xFF,0xFF,
    0xB0,0xB0,0xB0,
    0x60,0x60,0x60,
    0x00,0x00,0x00 */

/* Possibly needs a little more green in it */
	0xFF,0xFB,0x87,		/* Background */
	0xB1,0xAE,0x4E,		/* Light */
	0x84,0x80,0x4E,		/* Medium */
	0x4E,0x4E,0x4E,		/* Dark */

/* Palette for Game Boy Pocket/Light */
	0xC4,0xCF,0xA1,		/* Background */
	0x8B,0x95,0x6D,		/* Light      */
	0x6B,0x73,0x53,		/* Medium     */
	0x41,0x41,0x41,		/* Dark       */
};

static const unsigned char palette_megaduck[] = {
	0x6B, 0xA6, 0x4A, 0x43, 0x7A, 0x63, 0x25, 0x59, 0x55, 0x12, 0x42, 0x4C
};

/* Initialise the palettes */
PALETTE_INIT( gb )
{
	int ii;
	for( ii = 0; ii < 4; ii++)
	{
		palette_set_color_rgb(machine, ii, palette[ii*3+0], palette[ii*3+1], palette[ii*3+2]);
	}
}

PALETTE_INIT( gbp )
{
	int ii;
	for( ii = 0; ii < 4; ii++)
	{
		palette_set_color_rgb(machine, ii, palette[(ii + 4)*3+0], palette[(ii + 4)*3+1], palette[(ii + 4)*3+2]);
	}
}

PALETTE_INIT( sgb )
{
	gb_state *state = machine->driver_data<gb_state>();
	int ii, r, g, b;

	for( ii = 0; ii < 32768; ii++ )
	{
		r = (ii & 0x1F) << 3;
		g = ((ii >> 5) & 0x1F) << 3;
		b = ((ii >> 10) & 0x1F) << 3;
		palette_set_color_rgb(machine,  ii, r, g, b );
	}

	/* Some default colours for non-SGB games */
	state->sgb_pal[0] = 32767;
	state->sgb_pal[1] = 21140;
	state->sgb_pal[2] = 10570;
	state->sgb_pal[3] = 0;
	/* The rest of the colortable can be black */
	for( ii = 4; ii < 8*16; ii++ )
		state->sgb_pal[ii] = 0;
}

PALETTE_INIT( gbc )
{
	gb_state *state = machine->driver_data<gb_state>();
	int ii, r, g, b;

	for( ii = 0; ii < 32768; ii++ )
	{
		r = (ii & 0x1F) << 3;
		g = ((ii >> 5) & 0x1F) << 3;
		b = ((ii >> 10) & 0x1F) << 3;
		palette_set_color_rgb( machine, ii, r, g, b );
	}

	/* Background is initialised as white */
	for( ii = 0; ii < 32; ii++ )
		state->lcd.cgb_bpal[ii] = 32767;
	/* Sprites are supposed to be uninitialized, but we'll make them black */
	for( ii = 0; ii < 32; ii++ )
		state->lcd.cgb_spal[ii] = 0;
}

PALETTE_INIT( megaduck )
{
	int ii;
	for( ii = 0; ii < 4; ii++)
	{
		palette_set_color_rgb(machine, ii, palette_megaduck[ii*3+0], palette_megaduck[ii*3+1], palette_megaduck[ii*3+2]);
	}
}


INLINE void gb_plot_pixel(bitmap_t *bitmap, int x, int y, UINT32 color)
{
	*BITMAP_ADDR16(bitmap, y, x) = (UINT16)color;
}

/*
  Select which sprites should be drawn for the current scanline and return the
  number of sprites selected.
 */
static void gb_select_sprites( gb_state *state )
{
	int	i, /*yindex,*/ line, height;
	UINT8	*oam = state->lcd.gb_oam->base() + 39 * 4;

	state->lcd.sprCount = 0;

	/* If video hardware is enabled and sprites are enabled */
	if ( ( LCDCONT & 0x80 ) && ( LCDCONT & 0x02 ) )
	{
		/* Check for stretched sprites */
		if ( LCDCONT & 0x04 )
		{
			height = 16;
		}
		else
		{
			height = 8;
		}

		//yindex = state->lcd.current_line;
		line = state->lcd.current_line + 16;

		for( i = 39; i >= 0; i-- )
		{
			if ( line >= oam[0] && line < ( oam[0] + height ) && oam[1] && oam[1] < 168 )
			{
				/* We limit the sprite count to max 10 here;
                   proper games should not exceed this... */
				if ( state->lcd.sprCount < 10 )
				{
					state->lcd.sprite[state->lcd.sprCount] = i;
					state->lcd.sprCount++;
				}
			}
			oam -= 4;
		}
	}
}

INLINE void gb_update_sprites ( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;
	UINT8 height, tilemask, line, *oam, *vram;
	int i, yindex;

	if (LCDCONT & 0x04)
	{
		height = 16;
		tilemask = 0xFE;
	}
	else
	{
		height = 8;
		tilemask = 0xFF;
	}

	yindex = state->lcd.current_line;
	line = state->lcd.current_line + 16;

	oam = state->lcd.gb_oam->base() + 39 * 4;
	vram = state->lcd.gb_vram->base();
	for (i = 39; i >= 0; i--)
	{
		/* if sprite is on current line && x-coordinate && x-coordinate is < 168 */
		if (line >= oam[0] && line < (oam[0] + height) && oam[1] && oam[1] < 168)
		{
			UINT16 data;
			UINT8 bit, *spal;
			int xindex, adr;

			spal = (oam[3] & 0x10) ? state->lcd.gb_spal1 : state->lcd.gb_spal0;
			xindex = oam[1] - 8;
			if (oam[3] & 0x40)		   /* flip y ? */
			{
				adr = (oam[2] & tilemask) * 16 + (height - 1 - line + oam[0]) * 2;
			}
			else
			{
				adr = (oam[2] & tilemask) * 16 + (line - oam[0]) * 2;
			}
			data = (vram[adr + 1] << 8) | vram[adr];

			switch (oam[3] & 0xA0)
			{
			case 0xA0:				   /* priority is set (behind bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if (colour && !state->lcd.bg_zbuf[xindex] && xindex >= 0 && xindex < 160)
						gb_plot_pixel(bitmap, xindex, yindex, spal[colour]);
					data >>= 1;
				}
				break;
			case 0x20:				   /* priority is not set (overlaps bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if (colour && xindex >= 0 && xindex < 160)
						gb_plot_pixel(bitmap, xindex, yindex, spal[colour]);
					data >>= 1;
				}
				break;
			case 0x80:				   /* priority is set (behind bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8 && xindex < 160; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if (colour && !state->lcd.bg_zbuf[xindex] && xindex >= 0 && xindex < 160)
						gb_plot_pixel(bitmap, xindex, yindex, spal[colour]);
					data <<= 1;
				}
				break;
			case 0x00:				   /* priority is not set (overlaps bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8 && xindex < 160; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if (colour && xindex >= 0 && xindex < 160)
						gb_plot_pixel(bitmap, xindex, yindex, spal[colour]);
					data <<= 1;
				}
				break;
			}
		}
		oam -= 4;
	}
}

static void gb_update_scanline( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;

	g_profiler.start(PROFILER_VIDEO);

	/* Make sure we're in mode 3 */
	if ( ( LCDSTAT & 0x03 ) == 0x03 )
	{
		/* Calculate number of pixels to render based on time still left on the timer */
		UINT32 cycles_to_go = machine->device<cpu_device>("maincpu")->attotime_to_cycles(state->lcd.lcd_timer ->remaining( ) );
		int l = 0;

		if ( state->lcd.start_x < 0 )
		{
			/* Window is enabled if the hardware says so AND the current scanline is
             * within the window AND the window X coordinate is <=166 */
			state->lcd.layer[1].enabled = ( ( LCDCONT & 0x20 ) && ( state->lcd.current_line >= WNDPOSY ) && ( WNDPOSX <= 166 ) ) ? 1 : 0;

			/* BG is enabled if the hardware says so AND (window_off OR (window_on
            * AND window's X position is >=7 ) ) */
			state->lcd.layer[0].enabled = ( ( LCDCONT & 0x01 ) && ( ( ! state->lcd.layer[1].enabled ) || ( state->lcd.layer[1].enabled && ( WNDPOSX >= 7 ) ) ) ) ? 1 : 0;

			if ( state->lcd.layer[0].enabled )
			{
				state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
				state->lcd.layer[0].bg_map = state->lcd.gb_bgdtab;
				state->lcd.layer[0].bg_tiles = state->lcd.gb_chrgen;
				state->lcd.layer[0].xindex = SCROLLX >> 3;
				state->lcd.layer[0].xshift = SCROLLX & 7;
				state->lcd.layer[0].xstart = 0;
				state->lcd.layer[0].xend = 160;
			}

			if ( state->lcd.layer[1].enabled )
			{
				int xpos;

				xpos = WNDPOSX - 7;             /* Window is offset by 7 pixels */
				if ( xpos < 0 )
					xpos = 0;

				state->lcd.layer[1].bgline = state->lcd.window_lines_drawn;
				state->lcd.layer[1].bg_map = state->lcd.gb_wndtab;
				state->lcd.layer[1].bg_tiles = state->lcd.gb_chrgen;
				state->lcd.layer[1].xindex = 0;
				state->lcd.layer[1].xshift = 0;
				state->lcd.layer[1].xstart = xpos;
				state->lcd.layer[1].xend = 160;
				state->lcd.layer[0].xend = xpos;
			}
			state->lcd.start_x = 0;
		}

		if ( cycles_to_go < 160 )
		{
			state->lcd.end_x = MIN(160 - cycles_to_go,160);
			/* Draw empty pixels when the background is disabled */
			if ( ! ( LCDCONT & 0x01 ) )
			{
				rectangle r;
				r.min_y = r.max_y = state->lcd.current_line;
				r.min_x = state->lcd.start_x;
				r.max_x = state->lcd.end_x - 1;
				bitmap_fill( bitmap, &r , state->lcd.gb_bpal[0]);
			}
			while ( l < 2 )
			{
				UINT8	xindex, *map, *tiles;
				UINT16	data;
				int	i, tile_index;

				if ( ! state->lcd.layer[l].enabled )
				{
					l++;
					continue;
				}
				map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
				tiles = state->lcd.layer[l].bg_tiles + ( ( state->lcd.layer[l].bgline & 7 ) << 1 );
				xindex = state->lcd.start_x;
				if ( xindex < state->lcd.layer[l].xstart )
					xindex = state->lcd.layer[l].xstart;
				i = state->lcd.end_x;
				if ( i > state->lcd.layer[l].xend )
					i = state->lcd.layer[l].xend;
				i = i - xindex;

				tile_index = ( map[ state->lcd.layer[l].xindex ] ^ state->lcd.gb_tile_no_mod ) * 16;
				data = tiles[ tile_index ] | ( tiles[ tile_index+1 ] << 8 );
				data <<= state->lcd.layer[l].xshift;

				while ( i > 0 )
				{
					while ( ( state->lcd.layer[l].xshift < 8 ) && i )
					{
						register int colour = ( ( data & 0x8000 ) ? 2 : 0 ) | ( ( data & 0x0080 ) ? 1 : 0 );
						gb_plot_pixel( bitmap, xindex, state->lcd.current_line, state->lcd.gb_bpal[ colour ] );
						state->lcd.bg_zbuf[ xindex ] = colour;
						xindex++;
						data <<= 1;
						state->lcd.layer[l].xshift++;
						i--;
					}
					if ( state->lcd.layer[l].xshift == 8 )
					{
						/* Take possible changes to SCROLLY into account */
						if ( l == 0 )
						{
							state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
							map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
							tiles = state->lcd.layer[l].bg_tiles + ( ( state->lcd.layer[l].bgline & 7 ) << 1 );
						}

						state->lcd.layer[l].xindex = ( state->lcd.layer[l].xindex + 1 ) & 31;
						state->lcd.layer[l].xshift = 0;
						tile_index = ( map[ state->lcd.layer[l].xindex ] ^ state->lcd.gb_tile_no_mod ) * 16;
						data = tiles[ tile_index ] | ( tiles[ tile_index+1 ] << 8 );
					}
				}
				l++;
			}
			if ( state->lcd.end_x == 160 && LCDCONT & 0x02 )
			{
				gb_update_sprites(machine);
			}
			state->lcd.start_x = state->lcd.end_x;
		}
	}
	else
	{
		if ( ! ( LCDCONT & 0x80 ) )
		{
			/* Draw an empty line when LCD is disabled */
			if ( state->lcd.previous_line != state->lcd.current_line )
			{
				if ( state->lcd.current_line < 144 )
				{
					screen_device *screen = machine->first_screen();
					rectangle r1;
					const rectangle &r = screen->visible_area();
					r1.min_y = r1.max_y = state->lcd.current_line;
					r1.min_x = r.min_x;
					r1.max_x = r.max_x;
					bitmap_fill( bitmap, &r1 , 0);
				}
				state->lcd.previous_line = state->lcd.current_line;
			}
		}
	}

	g_profiler.stop();
}

/* --- Super Game Boy Specific --- */

INLINE void sgb_update_sprites (running_machine *machine)
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;
	UINT8 height, tilemask, line, *oam, *vram, pal;
	INT16 i, yindex;

	if (LCDCONT & 0x04)
	{
		height = 16;
		tilemask = 0xFE;
	}
	else
	{
		height = 8;
		tilemask = 0xFF;
	}

	/* Offset to center of screen */
	yindex = state->lcd.current_line + SGB_YOFFSET;
	line = state->lcd.current_line + 16;

	oam = state->lcd.gb_oam->base() + 39 * 4;
	vram = state->lcd.gb_vram->base();
	for (i = 39; i >= 0; i--)
	{
		/* if sprite is on current line && x-coordinate && x-coordinate is < 168 */
		if (line >= oam[0] && line < (oam[0] + height) && oam[1] && oam[1] < 168)
		{
			UINT16 data;
			UINT8 bit, *spal;
			INT16 xindex;
			int adr;

			spal = (oam[3] & 0x10) ? state->lcd.gb_spal1 : state->lcd.gb_spal0;
			xindex = oam[1] - 8;
			if (oam[3] & 0x40)		   /* flip y ? */
			{
				adr = (oam[2] & tilemask) * 16 + (height -1 - line + oam[0]) * 2;
			}
			else
			{
				adr = (oam[2] & tilemask) * 16 + (line - oam[0]) * 2;
			}
			data = (vram[adr + 1] << 8) | vram[adr];

			/* Find the palette to use */
			pal = state->sgb_pal_map[(xindex >> 3)][((yindex - SGB_YOFFSET) >> 3)] << 2;

			/* Offset to center of screen */
			xindex += SGB_XOFFSET;

			switch (oam[3] & 0xA0)
			{
			case 0xA0:				   /* priority is set (behind bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if ((xindex >= SGB_XOFFSET && xindex < SGB_XOFFSET + 160) && colour && !state->lcd.bg_zbuf[xindex - SGB_XOFFSET])
						gb_plot_pixel(bitmap, xindex, yindex, state->sgb_pal[pal + spal[colour]]);
					data >>= 1;
				}
				break;
			case 0x20:				   /* priority is not set (overlaps bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if ((xindex >= SGB_XOFFSET && xindex < SGB_XOFFSET + 160) && colour)
						gb_plot_pixel(bitmap, xindex, yindex, state->sgb_pal[pal + spal[colour]]);
					data >>= 1;
				}
				break;
			case 0x80:				   /* priority is set (behind bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if ((xindex >= SGB_XOFFSET && xindex < SGB_XOFFSET + 160) && colour && !state->lcd.bg_zbuf[xindex - SGB_XOFFSET])
						gb_plot_pixel(bitmap, xindex, yindex, state->sgb_pal[pal + spal[colour]]);
					data <<= 1;
				}
				break;
			case 0x00:				   /* priority is not set (overlaps bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if ((xindex >= SGB_XOFFSET && xindex < SGB_XOFFSET + 160) && colour)
						gb_plot_pixel(bitmap, xindex, yindex, state->sgb_pal[pal + spal[colour]]);
					data <<= 1;
				}
				break;
			}
		}
		oam -= 4;
	}
}

static void sgb_refresh_border(running_machine *machine)
{
	gb_state *state = machine->driver_data<gb_state>();
	UINT16 data, data2;
	UINT16 yidx, xidx, xindex;
	UINT8 *map, *tiles, *tiles2;
	UINT8 pal, i;
	bitmap_t *bitmap = machine->generic.tmpbitmap;

	map = state->sgb_tile_map - 64;

	for( yidx = 0; yidx < 224; yidx++ )
	{
		xindex = 0;
		map += (yidx % 8) ? 0 : 64;
		for( xidx = 0; xidx < 64; xidx+=2 )
		{
			if( map[xidx+1] & 0x80 ) /* Vertical flip */
				tiles = state->sgb_tile_data + ( ( 7 - ( yidx % 8 ) ) << 1 );
			else /* No vertical flip */
				tiles = state->sgb_tile_data + ( ( yidx % 8 ) << 1 );
			tiles2 = tiles + 16;

			pal = (map[xidx+1] & 0x1C) >> 2;
			if( pal == 0 )
				pal = 1;
			pal <<= 4;

			if( state->sgb_hack )
			{ /* A few games do weird stuff */
				UINT8 tileno = map[xidx];
				if( tileno >= 128 ) tileno = ((64 + tileno) % 128) + 128;
				else tileno = (64 + tileno) % 128;
				data = tiles[ tileno * 32 ] | ( tiles[ ( tileno * 32 ) + 1 ] << 8 );
				data2 = tiles2[ tileno * 32 ] | ( tiles2[ ( tileno * 32 ) + 1 ] << 8 );
			}
			else
			{
				data = tiles[ map[xidx] * 32 ] | ( tiles[ (map[xidx] * 32 ) + 1 ] << 8 );
				data2 = tiles2[ map[xidx] * 32 ] | ( tiles2[ (map[xidx] * 32 ) + 1 ] << 8 );
			}

			for( i = 0; i < 8; i++ )
			{
				register UINT8 colour;
				if( (map[xidx+1] & 0x40) )	/* Horizontal flip */
				{
					colour = ((data  & 0x0001) ? 1 : 0) | ((data  & 0x0100) ? 2 : 0) |
						 ((data2 & 0x0001) ? 4 : 0) | ((data2 & 0x0100) ? 8 : 0);
					data >>= 1;
					data2 >>= 1;
				}
				else	/* No horizontal flip */
				{
					colour = ((data  & 0x0080) ? 1 : 0) | ((data  & 0x8000) ? 2 : 0) |
						 ((data2 & 0x0080) ? 4 : 0) | ((data2 & 0x8000) ? 8 : 0);
					data <<= 1;
					data2 <<= 1;
				}
				/* A slight hack below so we don't draw over the GB screen.
                 * Drawing there is allowed, but due to the way we draw the
                 * scanline, it can obscure the screen even when it shouldn't.
                 */
				if( !((yidx >= SGB_YOFFSET && yidx < SGB_YOFFSET + 144) &&
					(xindex >= SGB_XOFFSET && xindex < SGB_XOFFSET + 160)) )
				{
					gb_plot_pixel(bitmap, xindex, yidx, state->sgb_pal[pal + colour]);
				}
				xindex++;
			}
		}
	}
}

static void sgb_update_scanline( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;

	g_profiler.start(PROFILER_VIDEO);

	if ( ( LCDSTAT & 0x03 ) == 0x03 )
	{
		/* Calcuate number of pixels to render based on time still left on the timer */
		UINT32 cycles_to_go = machine->device<cpu_device>("maincpu")->attotime_to_cycles(state->lcd.lcd_timer ->remaining( ) );
		int l = 0;

		if ( state->lcd.start_x < 0 )
		{
			/* Window is enabled if the hardware says so AND the current scanline is
             * within the window AND the window X coordinate is <=166 */
			state->lcd.layer[1].enabled = ((LCDCONT & 0x20) && state->lcd.current_line >= WNDPOSY && WNDPOSX <= 166) ? 1 : 0;

			/* BG is enabled if the hardware says so AND (window_off OR (window_on
             * AND window's X position is >=7 ) ) */
			state->lcd.layer[0].enabled = ((LCDCONT & 0x01) && ((!state->lcd.layer[1].enabled) || (state->lcd.layer[1].enabled && WNDPOSX >= 7))) ? 1 : 0;

			if ( state->lcd.layer[0].enabled )
			{
				state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
				state->lcd.layer[0].bg_map = state->lcd.gb_bgdtab;
				state->lcd.layer[0].bg_tiles = state->lcd.gb_chrgen;
				state->lcd.layer[0].xindex = SCROLLX >> 3;
				state->lcd.layer[0].xshift = SCROLLX & 7;
				state->lcd.layer[0].xstart = 0;
				state->lcd.layer[0].xend = 160;
			}

			if ( state->lcd.layer[1].enabled )
			{
				int xpos;

				/* Window X position is offset by 7 so we'll need to adjust */
				xpos = WNDPOSX - 7;
				if (xpos < 0)
					xpos = 0;

				state->lcd.layer[1].bgline = state->lcd.window_lines_drawn;
				state->lcd.layer[1].bg_map = state->lcd.gb_wndtab;
				state->lcd.layer[1].bg_tiles = state->lcd.gb_chrgen;
				state->lcd.layer[1].xindex = 0;
				state->lcd.layer[1].xshift = 0;
				state->lcd.layer[1].xstart = xpos;
				state->lcd.layer[1].xend = 160;
				state->lcd.layer[0].xend = xpos;
			}
			state->lcd.start_x = 0;
		}

		if ( cycles_to_go == 0 )
		{

			/* Does this belong here? or should it be moved to the else block */
			/* Handle SGB mask */
			switch( state->sgb_window_mask )
			{
			case 1: /* Freeze screen */
				return;
			case 2: /* Blank screen (black) */
				{
					rectangle r;
					r.min_x = SGB_XOFFSET;
					r.max_x = SGB_XOFFSET + 160-1;
					r.min_y = SGB_YOFFSET;
					r.max_y = SGB_YOFFSET + 144 - 1;
					bitmap_fill( bitmap, &r , 0);
				} return;
			case 3: /* Blank screen (white - or should it be color 0?) */
				{
					rectangle r;
					r.min_x = SGB_XOFFSET;
					r.max_x = SGB_XOFFSET + 160 - 1;
					r.min_y = SGB_YOFFSET;
					r.max_y = SGB_YOFFSET + 144 - 1;
					bitmap_fill( bitmap, &r , 32767);
				} return;
			}

			/* Draw the "border" if we're on the first line */
			if ( state->lcd.current_line == 0 )
			{
				sgb_refresh_border(machine);
			}
		}
		if ( cycles_to_go < 160 )
		{
			state->lcd.end_x = MIN(160 - cycles_to_go,160);

			/* if background or screen disabled clear line */
			if ( ! ( LCDCONT & 0x01 ) )
			{
				rectangle r;
				r.min_x = SGB_XOFFSET;
				r.max_x = SGB_XOFFSET + 160 - 1;
				r.min_y = r.max_y = state->lcd.current_line + SGB_YOFFSET;
				bitmap_fill( bitmap, &r , 0);
			}
			while( l < 2 )
			{
				UINT8	xindex, sgb_palette, *map, *tiles;
				UINT16	data;
				int	i, tile_index;

				if ( ! state->lcd.layer[l].enabled )
				{
					l++;
					continue;
				}
				map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
				tiles = state->lcd.layer[l].bg_tiles + ( ( state->lcd.layer[l].bgline & 7 ) << 1 );
				xindex = state->lcd.start_x;
				if ( xindex < state->lcd.layer[l].xstart )
					xindex = state->lcd.layer[l].xstart;
				i = state->lcd.end_x;
				if ( i > state->lcd.layer[l].xend )
					i = state->lcd.layer[l].xend;
				i = i - xindex;

				tile_index = (map[state->lcd.layer[l].xindex] ^ state->lcd.gb_tile_no_mod) * 16;
				data = tiles[tile_index] | ( tiles[tile_index + 1] << 8 );
				data <<= state->lcd.layer[l].xshift;

				/* Figure out which palette we're using */
				sgb_palette = state->sgb_pal_map[ ( state->lcd.end_x - i ) >> 3 ][ state->lcd.current_line >> 3 ] << 2;

				while( i > 0 )
				{
					while( ( state->lcd.layer[l].xshift < 8 ) && i )
					{
						register int colour = ( ( data & 0x8000 ) ? 2 : 0 ) | ( ( data & 0x0080 ) ? 1 : 0 );
						gb_plot_pixel( bitmap, xindex + SGB_XOFFSET, state->lcd.current_line + SGB_YOFFSET, state->sgb_pal[ sgb_palette + state->lcd.gb_bpal[colour]] );
						state->lcd.bg_zbuf[xindex] = colour;
						xindex++;
						data <<= 1;
						state->lcd.layer[l].xshift++;
						i--;
					}
					if ( state->lcd.layer[l].xshift == 8 )
					{
						/* Take possible changes to SCROLLY into account */
						if ( l == 0 )
						{
							state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
							map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
							tiles = state->lcd.layer[l].bg_tiles + ( ( state->lcd.layer[l].bgline & 7 ) << 1 );
						}

						state->lcd.layer[l].xindex = ( state->lcd.layer[l].xindex + 1 ) & 31;
						state->lcd.layer[l].xshift = 0;
						tile_index = ( map[ state->lcd.layer[l].xindex ] ^ state->lcd.gb_tile_no_mod ) * 16;
						data = tiles[ tile_index ] | ( tiles[ tile_index + 1 ] << 8 );
						sgb_palette = state->sgb_pal_map[ ( state->lcd.end_x - i ) >> 3 ][ state->lcd.current_line >> 3 ] << 2;
					}
				}
				l++;
			}
			if ( ( state->lcd.end_x == 160 ) && ( LCDCONT & 0x02 ) )
			{
				sgb_update_sprites(machine);
			}
			state->lcd.start_x = state->lcd.end_x;
		}
	}
	else
	{
		if ( ! ( LCDCONT * 0x80 ) )
		{
			/* if screen disabled clear line */
			if ( state->lcd.previous_line != state->lcd.current_line )
			{
				/* Also refresh border here??? */
				if ( state->lcd.current_line < 144 )
				{
					rectangle r;
					r.min_x = SGB_XOFFSET;
					r.max_x = SGB_XOFFSET + 160 - 1;
					r.min_y = r.max_y = state->lcd.current_line + SGB_YOFFSET;
					bitmap_fill(bitmap, &r, 0);
				}
				state->lcd.previous_line = state->lcd.current_line;
			}
		}
	}

	g_profiler.stop();
}

/* --- Game Boy Color Specific --- */

INLINE void cgb_update_sprites ( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;
	UINT8 height, tilemask, line, *oam;
	int i, xindex, yindex;

	if (LCDCONT & 0x04)
	{
		height = 16;
		tilemask = 0xFE;
	}
	else
	{
		height = 8;
		tilemask = 0xFF;
	}

	yindex = state->lcd.current_line;
	line = state->lcd.current_line + 16;

	oam = state->lcd.gb_oam->base() + 39 * 4;
	for (i = 39; i >= 0; i--)
	{
		/* if sprite is on current line && x-coordinate && x-coordinate is < 168 */
		if (line >= oam[0] && line < (oam[0] + height) && oam[1] && oam[1] < 168)
		{
			UINT16 data;
			UINT8 bit, pal;

			/* Handle mono mode for GB games */
			if( ! state->lcd.gbc_mode )
				pal = (oam[3] & 0x10) ? 4 : 0;
			else
				pal = ((oam[3] & 0x7) * 4);

			xindex = oam[1] - 8;
			if (oam[3] & 0x40)		   /* flip y ? */
			{
				data = *((UINT16 *) &state->lcd.gb_vram->base()[ ((oam[3] & 0x8)<<10) + (oam[2] & tilemask) * 16 + (height - 1 - line + oam[0]) * 2]);
			}
			else
			{
				data = *((UINT16 *) &state->lcd.gb_vram->base()[ ((oam[3] & 0x8)<<10) + (oam[2] & tilemask) * 16 + (line - oam[0]) * 2]);
			}
#ifndef LSB_FIRST
			data = (data << 8) | (data >> 8);
#endif

			switch (oam[3] & 0xA0)
			{
			case 0xA0:				   /* priority is set (behind bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if (colour && !state->lcd.bg_zbuf[xindex] && xindex >= 0 && xindex < 160)
					{
						if ( ! state->lcd.gbc_mode )
							colour = pal ? state->lcd.gb_spal1[colour] : state->lcd.gb_spal0[colour];
						gb_plot_pixel(bitmap, xindex, yindex, state->lcd.cgb_spal[pal + colour]);
					}
					data >>= 1;
				}
				break;
			case 0x20:				   /* priority is not set (overlaps bgnd & wnd, flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x0100) ? 2 : 0) | ((data & 0x0001) ? 1 : 0);
					if((state->lcd.bg_zbuf[xindex] & 0x80) && (state->lcd.bg_zbuf[xindex] & 0x7f) && (LCDCONT & 0x1))
						colour = 0;
					if (colour && xindex >= 0 && xindex < 160)
					{
						if ( ! state->lcd.gbc_mode )
							colour = pal ? state->lcd.gb_spal1[colour] : state->lcd.gb_spal0[colour];
						gb_plot_pixel(bitmap, xindex, yindex, state->lcd.cgb_spal[pal + colour]);
					}
					data >>= 1;
				}
				break;
			case 0x80:				   /* priority is set (behind bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if (colour && !state->lcd.bg_zbuf[xindex] && xindex >= 0 && xindex < 160)
					{
						if ( ! state->lcd.gbc_mode )
							colour = pal ? state->lcd.gb_spal1[colour] : state->lcd.gb_spal0[colour];
						gb_plot_pixel(bitmap, xindex, yindex, state->lcd.cgb_spal[pal + colour]);
					}
					data <<= 1;
				}
				break;
			case 0x00:				   /* priority is not set (overlaps bgnd & wnd, don't flip x) */
				for (bit = 0; bit < 8; bit++, xindex++)
				{
					register int colour = ((data & 0x8000) ? 2 : 0) | ((data & 0x0080) ? 1 : 0);
					if((state->lcd.bg_zbuf[xindex] & 0x80) && (state->lcd.bg_zbuf[xindex] & 0x7f) && (LCDCONT & 0x1))
						colour = 0;
					if (colour && xindex >= 0 && xindex < 160)
					{
						if ( ! state->lcd.gbc_mode )
							colour = pal ? state->lcd.gb_spal1[colour] : state->lcd.gb_spal0[colour];
						gb_plot_pixel(bitmap, xindex, yindex, state->lcd.cgb_spal[pal + colour]);
					}
					data <<= 1;
				}
				break;
			}
		}
		oam -= 4;
	}
}

static void cgb_update_scanline ( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	bitmap_t *bitmap = machine->generic.tmpbitmap;

	g_profiler.start(PROFILER_VIDEO);

	if ( ( LCDSTAT & 0x03 ) == 0x03 )
	{
		/* Calcuate number of pixels to render based on time still left on the timer */
		UINT32 cycles_to_go = machine->device<cpu_device>("maincpu")->attotime_to_cycles(state->lcd.lcd_timer ->remaining( ) );
		int l = 0;

		if ( state->lcd.start_x < 0 )
		{
			/* Window is enabled if the hardware says so AND the current scanline is
             * within the window AND the window X coordinate is <=166 */
			state->lcd.layer[1].enabled = ( ( LCDCONT & 0x20 ) && ( state->lcd.current_line >= WNDPOSY ) && ( WNDPOSX <= 166 ) ) ? 1 : 0;

			/* BG is enabled if the hardware says so AND (window_off OR (window_on
             * AND window's X position is >=7 ) ) */
			state->lcd.layer[0].enabled = ( ( LCDCONT & 0x01 ) && ( ( ! state->lcd.layer[1].enabled ) || ( state->lcd.layer[1].enabled && ( WNDPOSX >= 7 ) ) ) ) ? 1 : 0;

			if ( state->lcd.layer[0].enabled )
			{
				state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
				state->lcd.layer[0].bg_map = state->lcd.gb_bgdtab;
				state->lcd.layer[0].gbc_map = state->lcd.gbc_bgdtab;
				state->lcd.layer[0].xindex = SCROLLX >> 3;
				state->lcd.layer[0].xshift = SCROLLX & 7;
				state->lcd.layer[0].xstart = 0;
				state->lcd.layer[0].xend = 160;
			}

			if ( state->lcd.layer[1].enabled )
			{
				int xpos;

				/* Window X position is offset by 7 so we'll need to adust */
				xpos = WNDPOSX - 7;
				if (xpos < 0)
					xpos = 0;

				state->lcd.layer[1].bgline = state->lcd.window_lines_drawn;
				state->lcd.layer[1].bg_map = state->lcd.gb_wndtab;
				state->lcd.layer[1].gbc_map = state->lcd.gbc_wndtab;
				state->lcd.layer[1].xindex = 0;
				state->lcd.layer[1].xshift = 0;
				state->lcd.layer[1].xstart = xpos;
				state->lcd.layer[1].xend = 160;
				state->lcd.layer[0].xend = xpos;
			}
			state->lcd.start_x = 0;
		}

		if ( cycles_to_go < 160 )
		{
			state->lcd.end_x = MIN(160 - cycles_to_go,160);
			/* Draw empty line when the background is disabled */
			if ( ! ( LCDCONT & 0x01 ) )
			{
				rectangle r;
				r.min_y = r.max_y = state->lcd.current_line;
				r.min_x = state->lcd.start_x;
				r.max_x = state->lcd.end_x - 1;
				bitmap_fill( bitmap, &r , ( ! state->lcd.gbc_mode ) ? 0 : 32767);
			}
			while ( l < 2 )
			{
				UINT8	xindex, *map, *tiles, *gbcmap;
				UINT16	data;
				int	i, tile_index;

				if ( ! state->lcd.layer[l].enabled )
				{
					l++;
					continue;
				}
				map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
				gbcmap = state->lcd.layer[l].gbc_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
				tiles = ( gbcmap[ state->lcd.layer[l].xindex ] & 0x08 ) ? state->lcd.gbc_chrgen : state->lcd.gb_chrgen;

				/* Check for vertical flip */
				if ( gbcmap[ state->lcd.layer[l].xindex ] & 0x40 )
				{
					tiles += ( ( 7 - ( state->lcd.layer[l].bgline & 0x07 ) ) << 1 );
				}
				else
				{
					tiles += ( ( state->lcd.layer[l].bgline & 0x07 ) << 1 );
				}
				xindex = state->lcd.start_x;
				if ( xindex < state->lcd.layer[l].xstart )
					xindex = state->lcd.layer[l].xstart;
				i = state->lcd.end_x;
				if ( i > state->lcd.layer[l].xend )
					i = state->lcd.layer[l].xend;
				i = i - xindex;

				tile_index = ( map[ state->lcd.layer[l].xindex ] ^ state->lcd.gb_tile_no_mod ) * 16;
				data = tiles[ tile_index ] | ( tiles[ tile_index + 1 ] << 8 );
				/* Check for horinzontal flip */
				if ( gbcmap[ state->lcd.layer[l].xindex ] & 0x20 )
				{
					data >>= state->lcd.layer[l].xshift;
				}
				else
				{
					data <<= state->lcd.layer[l].xshift;
				}

				while ( i > 0 )
				{
					while ( ( state->lcd.layer[l].xshift < 8 ) && i )
					{
						int colour;
						/* Check for horinzontal flip */
						if ( gbcmap[ state->lcd.layer[l].xindex ] & 0x20 )
						{
							colour = ( ( data & 0x0100 ) ? 2 : 0 ) | ( ( data & 0x0001 ) ? 1 : 0 );
							data >>= 1;
						}
						else
						{
							colour = ( ( data & 0x8000 ) ? 2 : 0 ) | ( ( data & 0x0080 ) ? 1 : 0 );
							data <<= 1;
						}
						gb_plot_pixel( bitmap, xindex, state->lcd.current_line, state->lcd.cgb_bpal[ ( ! state->lcd.gbc_mode ) ? state->lcd.gb_bpal[colour] : ( ( ( gbcmap[ state->lcd.layer[l].xindex ] & 0x07 ) * 4 ) + colour ) ] );
						state->lcd.bg_zbuf[ xindex ] = colour + ( gbcmap[ state->lcd.layer[l].xindex ] & 0x80 );
						xindex++;
						state->lcd.layer[l].xshift++;
						i--;
					}
					if ( state->lcd.layer[l].xshift == 8 )
					{
						/* Take possible changes to SCROLLY into account */
						if ( l == 0 )
						{
							state->lcd.layer[0].bgline = ( SCROLLY + state->lcd.current_line ) & 0xFF;
							map = state->lcd.layer[l].bg_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
							gbcmap = state->lcd.layer[l].gbc_map + ( ( state->lcd.layer[l].bgline << 2 ) & 0x3E0 );
						}

						state->lcd.layer[l].xindex = ( state->lcd.layer[l].xindex + 1 ) & 31;
						state->lcd.layer[l].xshift = 0;
						tiles = ( gbcmap[ state->lcd.layer[l].xindex ] & 0x08 ) ? state->lcd.gbc_chrgen : state->lcd.gb_chrgen;

						/* Check for vertical flip */
						if ( gbcmap[ state->lcd.layer[l].xindex ] & 0x40 )
						{
							tiles += ( ( 7 - ( state->lcd.layer[l].bgline & 0x07 ) ) << 1 );
						}
						else
						{
							tiles += ( ( state->lcd.layer[l].bgline & 0x07 ) << 1 );
						}
						tile_index = ( map[ state->lcd.layer[l].xindex ] ^ state->lcd.gb_tile_no_mod ) * 16;
						data = tiles[ tile_index ] | ( tiles[ tile_index + 1 ] << 8 );
					}
				}
				l++;
			}
			if ( state->lcd.end_x == 160 && ( LCDCONT & 0x02 ) )
			{
				cgb_update_sprites( machine );
			}
			state->lcd.start_x = state->lcd.end_x;
		}
	}
	else
	{
		if ( ! ( LCDCONT & 0x80 ) )
		{
			/* Draw an empty line when LCD is disabled */
			if ( state->lcd.previous_line != state->lcd.current_line )
			{
				if ( state->lcd.current_line < 144 )
				{
					screen_device *screen = machine->first_screen();
					rectangle r;
					const rectangle &r1 = screen->visible_area();
					r.min_x = r1.min_x;
					r.max_x = r1.max_x;
					r.min_y = r.max_y = state->lcd.current_line;
					bitmap_fill( bitmap, &r , ( ! state->lcd.gbc_mode ) ? 0 : 32767);
				}
				state->lcd.previous_line = state->lcd.current_line;
			}
		}
	}

	g_profiler.stop();
}

/* OAM contents on power up.

The OAM area seems contain some kind of unit fingerprint. On each boot
the data is almost always the same. Some random bits are flipped between
different boots. It is currently unknown how much these fingerprints
differ between different units.

OAM fingerprints taken from Wilbert Pol's own unit.
*/

static const UINT8 dmg_oam_fingerprint[0x100] = {
	0xD8, 0xE6, 0xB3, 0x89, 0xEC, 0xDE, 0x11, 0x62, 0x0B, 0x7E, 0x48, 0x9E, 0xB9, 0x6E, 0x26, 0xC9,
	0x36, 0xF4, 0x7D, 0xE4, 0xD9, 0xCE, 0xFA, 0x5E, 0xA3, 0x77, 0x60, 0xFC, 0x1C, 0x64, 0x8B, 0xAC,
	0xB6, 0x74, 0x3F, 0x9A, 0x0E, 0xFE, 0xEA, 0xA9, 0x40, 0x3A, 0x7A, 0xB6, 0xF2, 0xED, 0xA8, 0x3E,
	0xAF, 0x2C, 0xD2, 0xF2, 0x01, 0xE0, 0x5B, 0x3A, 0x53, 0x6A, 0x1C, 0x6C, 0x20, 0xD9, 0x22, 0xB4,
	0x8C, 0x38, 0x71, 0x69, 0x3E, 0x93, 0xA3, 0x22, 0xCE, 0x76, 0x24, 0xE7, 0x1A, 0x14, 0x6B, 0xB1,
	0xF9, 0x3D, 0xBF, 0x3D, 0x74, 0x64, 0xCB, 0xF5, 0xDC, 0x9A, 0x53, 0xC6, 0x0E, 0x78, 0x34, 0xCB,
	0x42, 0xB3, 0xFF, 0x07, 0x73, 0xAE, 0x6C, 0xA2, 0x6F, 0x6A, 0xA4, 0x66, 0x0A, 0x8C, 0x40, 0xB3,
	0x9A, 0x3D, 0x39, 0x78, 0xAB, 0x29, 0xE7, 0xC5, 0x7A, 0xDD, 0x51, 0x95, 0x2B, 0xE4, 0x1B, 0xF6,
	0x31, 0x16, 0x34, 0xFE, 0x11, 0xF2, 0x5E, 0x11, 0xF3, 0x95, 0x66, 0xB9, 0x37, 0xC2, 0xAD, 0x6D,
	0x1D, 0xA7, 0x79, 0x06, 0xD7, 0xE5, 0x8F, 0xFA, 0x9C, 0x02, 0x0C, 0x31, 0x8B, 0x17, 0x2E, 0x31,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const UINT8 mgb_oam_fingerprint[0x100] = {
	0xB9, 0xE9, 0x0D, 0x69, 0xBB, 0x7F, 0x00, 0x80, 0xE9, 0x7B, 0x79, 0xA2, 0xFD, 0xCF, 0xD8, 0x0A,
	0x87, 0xEF, 0x44, 0x11, 0xFE, 0x37, 0x10, 0x21, 0xFA, 0xFF, 0x00, 0x17, 0xF6, 0x4F, 0x83, 0x03,
	0x3A, 0xF4, 0x00, 0x24, 0xBB, 0xAE, 0x05, 0x01, 0xFF, 0xF7, 0x12, 0x48, 0xA7, 0x5E, 0xF6, 0x28,
	0x5B, 0xFF, 0x2E, 0x10, 0xFF, 0xB9, 0x50, 0xC8, 0xAF, 0x77, 0x2C, 0x1A, 0x62, 0xD7, 0x81, 0xC2,
	0xFD, 0x5F, 0xA0, 0x94, 0xAF, 0xFF, 0x51, 0x20, 0x36, 0x76, 0x50, 0x0A, 0xFD, 0xF6, 0x20, 0x00,
	0xFE, 0xF7, 0xA0, 0x68, 0xFF, 0xFC, 0x29, 0x51, 0xA3, 0xFA, 0x06, 0xC4, 0x94, 0xFF, 0x39, 0x0A,
	0xFF, 0x6C, 0x20, 0x20, 0xF1, 0xAD, 0x0C, 0x81, 0x56, 0xFB, 0x03, 0x82, 0xFF, 0xFF, 0x08, 0x58,
	0x96, 0x7E, 0x01, 0x4D, 0xFF, 0xE4, 0x82, 0xE3, 0x3D, 0xBB, 0x54, 0x00, 0x3D, 0xF3, 0x04, 0x21,
	0xB7, 0x39, 0xCC, 0x10, 0xF9, 0x5B, 0x80, 0x50, 0x3F, 0x6A, 0x1C, 0x21, 0x1F, 0xFA, 0xA8, 0x52,
	0x5F, 0xB3, 0x44, 0xA1, 0x96, 0x1E, 0x00, 0x27, 0x63, 0x77, 0x30, 0x54, 0x37, 0x6F, 0x60, 0x22,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const UINT8 cgb_oam_fingerprint[0x100] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x74, 0xFF, 0x09, 0x00, 0x9D, 0x61, 0xA8, 0x28, 0x36, 0x1E, 0x58, 0xAA, 0x75, 0x74, 0xA1, 0x42,
	0x05, 0x96, 0x40, 0x09, 0x41, 0x02, 0x60, 0x00, 0x1F, 0x11, 0x22, 0xBC, 0x31, 0x52, 0x22, 0x54,
	0x22, 0xA9, 0xC4, 0x00, 0x1D, 0xAD, 0x80, 0x0C, 0x5D, 0xFA, 0x51, 0x92, 0x93, 0x98, 0xA4, 0x04,
	0x22, 0xA9, 0xC4, 0x00, 0x1D, 0xAD, 0x80, 0x0C, 0x5D, 0xFA, 0x51, 0x92, 0x93, 0x98, 0xA4, 0x04,
	0x22, 0xA9, 0xC4, 0x00, 0x1D, 0xAD, 0x80, 0x0C, 0x5D, 0xFA, 0x51, 0x92, 0x93, 0x98, 0xA4, 0x04,
	0x22, 0xA9, 0xC4, 0x00, 0x1D, 0xAD, 0x80, 0x0C, 0x5D, 0xFA, 0x51, 0x92, 0x93, 0x98, 0xA4, 0x04
};

/*
  For an AGS in CGB mode this data is: */
#if 0
static const UINT8 abs_oam_fingerprint[0x100] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
    0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD, 0xDD,
    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
#endif

enum {
	GB_LCD_STATE_LYXX_M3=1,
	GB_LCD_STATE_LYXX_PRE_M0,
	GB_LCD_STATE_LYXX_M0,
	GB_LCD_STATE_LYXX_M0_SCX3,
	GB_LCD_STATE_LYXX_M0_GBC_PAL,
	GB_LCD_STATE_LYXX_M0_PRE_INC,
	GB_LCD_STATE_LYXX_M0_INC,
	GB_LCD_STATE_LY00_M2,
	GB_LCD_STATE_LYXX_M2,
	GB_LCD_STATE_LY9X_M1,
	GB_LCD_STATE_LY9X_M1_INC,
	GB_LCD_STATE_LY00_M1,
	GB_LCD_STATE_LY00_M1_1,
	GB_LCD_STATE_LY00_M1_2,
	GB_LCD_STATE_LY00_M0
};

static TIMER_CALLBACK( gb_video_init_vbl )
{
	cputag_set_input_line( machine, "maincpu", VBL_INT, ASSERT_LINE );
}

MACHINE_START( gb_video )
{
	gb_state *state = machine->driver_data<gb_state>();
	state->lcd.lcd_timer = machine->scheduler().timer_alloc(FUNC(gb_lcd_timer_proc));
}

MACHINE_START( gbc_video )
{
	gb_state *state = machine->driver_data<gb_state>();
	state->lcd.lcd_timer = machine->scheduler().timer_alloc(FUNC(gbc_lcd_timer_proc));
}

void gb_video_reset( running_machine *machine, int mode )
{
	gb_state *state = machine->driver_data<gb_state>();
	int	i;
	int vram_size = 0x2000;
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	emu_timer *old_timer = state->lcd.lcd_timer;

	memset( &state->lcd, 0, sizeof(state->lcd) );
	state->lcd.lcd_timer = old_timer;

	if (mode == GB_VIDEO_CGB) vram_size = 0x4000;

	/* free regions if already allocated */
	if (machine->region("gfx1")->base())		machine->region_free("gfx1");
	if (machine->region("gfx2")->base())		machine->region_free("gfx2");

	state->lcd.gb_vram = machine->region_alloc("gfx1", vram_size, 0 );
	state->lcd.gb_oam = machine->region_alloc("gfx2", 0x100, 0 );
	memset( state->lcd.gb_vram->base(), 0, vram_size );

	state->lcd.gb_vram_ptr = state->lcd.gb_vram->base();
	state->lcd.gb_chrgen = state->lcd.gb_vram->base();
	state->lcd.gb_bgdtab = state->lcd.gb_vram->base() + 0x1C00;
	state->lcd.gb_wndtab = state->lcd.gb_vram->base() + 0x1C00;

	state->lcd.gb_vid_regs[0x06] = 0xFF;
	for( i = 0x0c; i < _NR_GB_VID_REGS; i++ )
	{
		state->lcd.gb_vid_regs[i] = 0xFF;
	}

	LCDSTAT = 0x80;
	LCDCONT = 0x00;		/* Video hardware is turned off at boot time */
	state->lcd.current_line = CURLINE = CMPLINE = 0x00;
	SCROLLX = SCROLLY = 0x00;
	SPR0PAL = SPR1PAL = 0xFF;
	WNDPOSX = WNDPOSY = 0x00;

	/* Initialize palette arrays */
	for( i = 0; i < 4; i++ )
	{
		state->lcd.gb_bpal[i] = state->lcd.gb_spal0[i] = state->lcd.gb_spal1[i] = i;
	}

	switch( mode )
	{
	case GB_VIDEO_DMG:
		state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(456));

		/* set the scanline update function */
		state->update_scanline = gb_update_scanline;

		memcpy( state->lcd.gb_oam->base(), dmg_oam_fingerprint, 0x100 );

		break;
	case GB_VIDEO_MGB:
		/* set the scanline update function */
		state->update_scanline = gb_update_scanline;
		/* Initialize part of VRAM. This code must be deleted when we have added the bios dump */
		for( i = 1; i < 0x0D; i++ )
		{
			state->lcd.gb_vram->base()[ 0x1903 + i ] = i;
			state->lcd.gb_vram->base()[ 0x1923 + i ] = i + 0x0C;
		}
		state->lcd.gb_vram->base()[ 0x1910 ] = 0x19;


		memcpy( state->lcd.gb_oam->base(), mgb_oam_fingerprint, 0x100 );

		/* Make sure the VBlank interrupt is set when the first instruction gets executed */
		machine->scheduler().timer_set(machine->device<cpu_device>("maincpu")->cycles_to_attotime(1), FUNC(gb_video_init_vbl));

		/* Initialize some video registers */
		gb_video_w( space, 0x0, 0x91 );    /* LCDCONT */
		gb_video_w( space, 0x7, 0xFC );    /* BGRDPAL */
		gb_video_w( space, 0x8, 0xFC );    /* SPR0PAL */
		gb_video_w( space, 0x9, 0xFC );    /* SPR1PAL */

		CURLINE = state->lcd.current_line = 0;
		LCDSTAT = ( LCDSTAT & 0xF8 ) | 0x05;
		state->lcd.mode = 1;
		state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(60), GB_LCD_STATE_LY00_M0);

		break;
	case GB_VIDEO_SGB:
		/* set the scanline update function */
		state->update_scanline = sgb_update_scanline;

		break;

	case GB_VIDEO_CGB:
		/* set the scanline update function */
		state->update_scanline = cgb_update_scanline;

		memcpy( state->lcd.gb_oam->base(), cgb_oam_fingerprint, 0x100 );

		state->lcd.gb_chrgen = state->lcd.gb_vram->base();
		state->lcd.gbc_chrgen = state->lcd.gb_vram->base() + 0x2000;
		state->lcd.gb_bgdtab = state->lcd.gb_wndtab = state->lcd.gb_vram->base() + 0x1C00;
		state->lcd.gbc_bgdtab = state->lcd.gbc_wndtab = state->lcd.gb_vram->base() + 0x3C00;

		/* HDMA disabled */
		state->lcd.hdma_enabled = 0;
		state->lcd.hdma_possible = 0;

		state->lcd.gbc_mode = 1;
		break;
	}
}

static void gbc_hdma(running_machine *machine, UINT16 length)
{
	gb_state *state = machine->driver_data<gb_state>();
	UINT16 src, dst;
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);

	src = ((UINT16)HDMA1 << 8) | (HDMA2 & 0xF0);
	dst = ((UINT16)(HDMA3 & 0x1F) << 8) | (HDMA4 & 0xF0);
	dst |= 0x8000;
	while( length > 0 )
	{
		space->write_byte( dst++, space->read_byte( src++ ) );
		length--;
	}
	HDMA1 = src >> 8;
	HDMA2 = src & 0xF0;
	HDMA3 = 0x1f & (dst >> 8);
	HDMA4 = dst & 0xF0;
	HDMA5--;
	if( (HDMA5 & 0x7f) == 0x7f )
	{
		HDMA5 = 0xff;
		state->lcd.hdma_enabled = 0;
	}
}

static void gb_increment_scanline( gb_state *state )
{
	state->lcd.current_line = ( state->lcd.current_line + 1 ) % 154;
	if ( LCDCONT & 0x80 )
	{
		CURLINE = state->lcd.current_line;
	}
	if ( state->lcd.current_line == 0 )
	{
		state->lcd.window_lines_drawn = 0;
	}
}

static TIMER_CALLBACK(gb_lcd_timer_proc)
{
	gb_state *state = machine->driver_data<gb_state>();
	static const int sprite_cycles[] = { 0, 8, 20, 32, 44, 52, 64, 76, 88, 96, 108 };

	state->lcd.state = param;

	if ( LCDCONT & 0x80 )
	{
		switch( state->lcd.state )
		{
		case GB_LCD_STATE_LYXX_PRE_M0:	/* Just before switching to mode 0 */
			state->lcd.mode = 0;
			if ( LCDSTAT & 0x08 )
			{
				if ( ! state->lcd.mode_irq )
				{
					if ( ! state->lcd.line_irq && ! state->lcd.delayed_line_irq )
					{
						state->lcd.mode_irq = 1;
						cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
					}
				}
				else
				{
					state->lcd.mode_irq = 0;
				}
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0);
			break;
		case GB_LCD_STATE_LYXX_M0:		/* Switch to mode 0 */
			/* update current scanline */
			(*state->update_scanline)( machine );
			/* Increment the number of window lines drawn if enabled */
			if ( state->lcd.layer[1].enabled )
			{
				state->lcd.window_lines_drawn++;
			}
			state->lcd.previous_line = state->lcd.current_line;
			/* Set Mode 0 lcdstate */
			state->lcd.mode = 0;
			LCDSTAT &= 0xFC;
			state->lcd.oam_locked = UNLOCKED;
			state->lcd.vram_locked = UNLOCKED;
			/*
                There seems to a kind of feature in the Game Boy hardware when the lowest bits of the
                SCROLLX register equals 3 or 7, then the delayed M0 irq is triggered 4 cycles later
                than usual.
                The SGB probably has the same bug.
            */
			if ( ( SCROLLX & 0x03 ) == 0x03 )
			{
				state->lcd.scrollx_adjust += 4;
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0_SCX3);
				break;
			}
		case GB_LCD_STATE_LYXX_M0_SCX3:
			/* Generate lcd interrupt if requested */
			if ( ! state->lcd.mode_irq && ( LCDSTAT & 0x08 ) &&
			     ( ( ! state->lcd.line_irq && state->lcd.delayed_line_irq ) || ! ( LCDSTAT & 0x40 ) ) )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(196 - state->lcd.scrollx_adjust - state->lcd.sprite_cycles), GB_LCD_STATE_LYXX_M0_PRE_INC);
			break;
		case GB_LCD_STATE_LYXX_M0_PRE_INC:	/* Just before incrementing the line counter go to mode 2 internally */
			if ( CURLINE < 143 )
			{
				state->lcd.mode = 2;
				state->lcd.triggering_mode_irq = ( LCDSTAT & 0x20 ) ? 1 : 0;
				if ( state->lcd.triggering_mode_irq )
				{
					if ( ! state->lcd.mode_irq )
					{
						if ( ! state->lcd.line_irq && ! state->lcd.delayed_line_irq )
						{
							state->lcd.mode_irq = 1;
							cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
						}
					}
					else
					{
						state->lcd.mode_irq = 0;
					}
				}
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0_INC);
			break;
		case GB_LCD_STATE_LYXX_M0_INC:	/* Increment LY, stay in M0 for 4 more cycles */
			gb_increment_scanline(state);
			state->lcd.delayed_line_irq = state->lcd.line_irq;
			state->lcd.triggering_line_irq = ( ( CMPLINE == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			if ( ! state->lcd.mode_irq && ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq && ! state->lcd.triggering_mode_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/* Reset LY==LYC STAT bit */
			LCDSTAT &= 0xFB;
			/* Check if we're going into VBlank next */
			if ( CURLINE == 144 )
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY9X_M1);
			}
			else
			{
				/* Internally switch to mode 2 */
				state->lcd.mode = 2;
				/* Generate lcd interrupt if requested */
				if ( ! state->lcd.mode_irq && state->lcd.triggering_mode_irq &&
					 ( ( ! state->lcd.triggering_line_irq && ! state->lcd.delayed_line_irq ) || ! ( LCDSTAT & 0x40 ) ) )
				{
					state->lcd.mode_irq = 1;
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M2);
			}
			break;
		case GB_LCD_STATE_LY00_M2:		/* Switch to mode 2 on line #0 */
			/* Set Mode 2 lcdstate */
			state->lcd.mode = 2;
			LCDSTAT = ( LCDSTAT & 0xFC ) | 0x02;
			state->lcd.oam_locked = LOCKED;
			/* Generate lcd interrupt if requested */
			if ( ( LCDSTAT & 0x20 ) && ! state->lcd.line_irq && ! state->lcd.line_irq )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/* Check for regular compensation of x-scroll register */
			state->lcd.scrollx_adjust = ( SCROLLX & 0x04 ) ? 4 : 0;
			/* Mode 2 lasts approximately 80 clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(80), GB_LCD_STATE_LYXX_M3);
			break;
		case GB_LCD_STATE_LYXX_M2:		/* Switch to mode 2 */
			/* Update STAT register to the correct state */
			LCDSTAT = (LCDSTAT & 0xFC) | 0x02;
			state->lcd.oam_locked = LOCKED;
			/* Generate lcd interrupt if requested */
			if ( ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq && ! ( LCDSTAT & 0x20 ) ) ||
				 ( ! state->lcd.mode_irq && ! state->lcd.line_irq && ! state->lcd.delayed_line_irq && state->lcd.triggering_mode_irq ) )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.line_irq = state->lcd.triggering_line_irq;
			state->lcd.triggering_mode_irq = 0;
			/* Check if LY==LYC STAT bit should be set */
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			/* Check for regular compensation of x-scroll register */
			state->lcd.scrollx_adjust = ( SCROLLX & 0x04 ) ? 4 : 0;
			/* Mode 2 last for approximately 80 clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(80), GB_LCD_STATE_LYXX_M3);
			break;
		case GB_LCD_STATE_LYXX_M3:		/* Switch to mode 3 */
			gb_select_sprites(state);
			state->lcd.sprite_cycles = sprite_cycles[ state->lcd.sprCount ];
			/* Set Mode 3 lcdstate */
			state->lcd.mode = 3;
			LCDSTAT = (LCDSTAT & 0xFC) | 0x03;
			state->lcd.vram_locked = LOCKED;
			/* Check for compensations of x-scroll register */
			/* Mode 3 lasts for approximately 172+cycles needed to handle sprites clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(168 + state->lcd.scrollx_adjust + state->lcd.sprite_cycles), GB_LCD_STATE_LYXX_PRE_M0);
			state->lcd.start_x = -1;
			break;
		case GB_LCD_STATE_LY9X_M1:		/* Switch to or stay in mode 1 */
			if ( CURLINE == 144 )
			{
				/* Trigger VBlank interrupt */
				cputag_set_input_line( machine, "maincpu", VBL_INT, ASSERT_LINE );
				/* Set VBlank lcdstate */
				state->lcd.mode = 1;
				LCDSTAT = (LCDSTAT & 0xFC) | 0x01;
				/* Trigger LCD interrupt if requested */
				if ( LCDSTAT & 0x10 )
				{
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
			/* Check if LY==LYC STAT bit should be set */
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			if ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(452), GB_LCD_STATE_LY9X_M1_INC);
			break;
		case GB_LCD_STATE_LY9X_M1_INC:		/* Increment scanline counter */
			gb_increment_scanline(state);
			state->lcd.delayed_line_irq = state->lcd.line_irq;
			state->lcd.triggering_line_irq = ( ( CMPLINE == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			if ( ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/* Reset LY==LYC STAT bit */
			LCDSTAT &= 0xFB;
			if ( state->lcd.current_line == 153 )
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M1);
			}
			else
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY9X_M1);
			}
			break;
		case GB_LCD_STATE_LY00_M1:		/* we stay in VBlank but current line counter should already be incremented */
			/* Check LY=LYC for line #153 */
			if ( state->lcd.delayed_line_irq )
			{
				if ( state->lcd.triggering_line_irq )
				{
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
			state->lcd.delayed_line_irq = state->lcd.delayed_line_irq | state->lcd.line_irq;
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			gb_increment_scanline(state);
			state->lcd.triggering_line_irq = ( ( CMPLINE == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			LCDSTAT &= 0xFB;
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4/*8*/), GB_LCD_STATE_LY00_M1_1);
			break;
		case GB_LCD_STATE_LY00_M1_1:
			if ( ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M1_2);
			break;
		case GB_LCD_STATE_LY00_M1_2:	/* Rest of line #0 during VBlank */
			if ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(444), GB_LCD_STATE_LY00_M0);
			break;
		case GB_LCD_STATE_LY00_M0:		/* The STAT register seems to go to 0 for about 4 cycles */
			/* Set Mode 0 lcdstat */
			state->lcd.mode = 0;
			LCDSTAT = ( LCDSTAT & 0xFC );
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M2);
			break;
		}
	}
	else
	{
		gb_increment_scanline(state);
		if ( state->lcd.current_line < 144 )
		{
			(*state->update_scanline)( machine );
		}
		state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(456));
	}
}

static TIMER_CALLBACK(gbc_lcd_timer_proc)
{
	gb_state *state = machine->driver_data<gb_state>();
	static const int sprite_cycles[] = { 0, 8, 20, 32, 44, 52, 64, 76, 88, 96, 108 };

	state->lcd.state = param;

	if ( LCDCONT & 0x80 )
	{
		switch( state->lcd.state )
		{
		case GB_LCD_STATE_LYXX_PRE_M0:	/* Just before switching to mode 0 */
			state->lcd.mode = 0;
			if ( LCDSTAT & 0x08 )
			{
				if ( ! state->lcd.mode_irq )
				{
					if ( ! state->lcd.line_irq && ! state->lcd.delayed_line_irq )
					{
						state->lcd.mode_irq = 1;
						cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
					}
				}
				else
				{
					state->lcd.mode_irq = 0;
				}
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0);
			break;
		case GB_LCD_STATE_LYXX_M0:		/* Switch to mode 0 */
			/* update current scanline */
			(*state->update_scanline)( machine );
			/* Increment the number of window lines drawn if enabled */
			if ( state->lcd.layer[1].enabled )
			{
				state->lcd.window_lines_drawn++;
			}
			state->lcd.previous_line = state->lcd.current_line;
			/* Set Mode 0 lcdstate */
			state->lcd.mode = 0;
			LCDSTAT &= 0xFC;
			state->lcd.oam_locked = UNLOCKED;
			state->lcd.vram_locked = UNLOCKED;
			/*
                There seems to a kind of feature in the Game Boy hardware when the lowest bits of the
                SCROLLX register equals 3 or 7, then the delayed M0 irq is triggered 4 cycles later
                than usual.
                The SGB probably has the same bug.
            */
			state->lcd.triggering_mode_irq = ( LCDSTAT & 0x08 ) ? 1 : 0;
			if ( ( SCROLLX & 0x03 ) == 0x03 )
			{
				state->lcd.scrollx_adjust += 4;
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0_SCX3);
				break;
			}
		case GB_LCD_STATE_LYXX_M0_SCX3:
			/* Generate lcd interrupt if requested */
			if ( ! state->lcd.mode_irq && state->lcd.triggering_mode_irq &&
			     ( ( ! state->lcd.line_irq && state->lcd.delayed_line_irq ) || ! ( LCDSTAT & 0x40 ) ) )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				state->lcd.triggering_mode_irq = 0;
			}
			if ( ( SCROLLX & 0x03 ) == 0x03 )
			{
				state->lcd.pal_locked = UNLOCKED;
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0_GBC_PAL);
			break;
		case GB_LCD_STATE_LYXX_M0_GBC_PAL:
			state->lcd.pal_locked = UNLOCKED;
            /* Check for HBLANK DMA */
			if( state->lcd.hdma_enabled )
			{
				gbc_hdma(machine, 0x10);
//              cpunum_set_reg( 0, LR35902_DMA_CYCLES, 36 );
			}
			else
			{
				state->lcd.hdma_possible = 1;
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(192 - state->lcd.scrollx_adjust - state->lcd.sprite_cycles), GB_LCD_STATE_LYXX_M0_PRE_INC);
			break;
		case GB_LCD_STATE_LYXX_M0_PRE_INC:	/* Just before incrementing the line counter go to mode 2 internally */
			state->lcd.cmp_line = CMPLINE;
			if ( CURLINE < 143 )
			{
				state->lcd.mode = 2;
				if ( LCDSTAT & 0x20 )
				{
					if ( ! state->lcd.mode_irq )
					{
						if ( ! state->lcd.line_irq && ! state->lcd.delayed_line_irq )
						{
							state->lcd.mode_irq = 1;
							cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
						}
					}
					else
					{
						state->lcd.mode_irq = 0;
					}
				}
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M0_INC);
			break;
		case GB_LCD_STATE_LYXX_M0_INC:	/* Increment LY, stay in M0 for 4 more cycles */
			gb_increment_scanline(state);
			state->lcd.delayed_line_irq = state->lcd.line_irq;
			state->lcd.triggering_line_irq = ( ( state->lcd.cmp_line == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			if ( ! state->lcd.mode_irq && ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq && ! ( LCDSTAT & 0x20 ) )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.hdma_possible = 0;
			/* Check if we're going into VBlank next */
			if ( CURLINE == 144 )
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY9X_M1);
			}
			else
			{
				/* Internally switch to mode 2 */
				state->lcd.mode = 2;
				/* Generate lcd interrupt if requested */
				if ( ! state->lcd.mode_irq && ( LCDSTAT & 0x20 ) &&
					 ( ( ! state->lcd.triggering_line_irq && ! state->lcd.delayed_line_irq ) || ! ( LCDSTAT & 0x40 ) ) )
				{
					state->lcd.mode_irq = 1;
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LYXX_M2);
			}
			break;
		case GB_LCD_STATE_LY00_M2:		/* Switch to mode 2 on line #0 */
			/* Set Mode 2 lcdstate */
			state->lcd.mode = 2;
			LCDSTAT = ( LCDSTAT & 0xFC ) | 0x02;
			state->lcd.oam_locked = LOCKED;
			/* Generate lcd interrupt if requested */
			if ( ( LCDSTAT & 0x20 ) && ! state->lcd.line_irq && ! state->lcd.line_irq )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/* Check for regular compensation of x-scroll register */
			state->lcd.scrollx_adjust = ( SCROLLX & 0x04 ) ? 4 : 0;
			/* Mode 2 lasts approximately 80 clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(80), GB_LCD_STATE_LYXX_M3);
			break;
		case GB_LCD_STATE_LYXX_M2:		/* Switch to mode 2 */
			/* Update STAT register to the correct state */
			LCDSTAT = (LCDSTAT & 0xFC) | 0x02;
			state->lcd.oam_locked = LOCKED;
			/* Generate lcd interrupt if requested */
			if ( ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq && ! ( LCDSTAT & 0x20 ) ) ||
				 ( !state->lcd.mode_irq && ! state->lcd.line_irq && ! state->lcd.delayed_line_irq && ( LCDSTAT & 0x20 ) ) )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.line_irq = state->lcd.triggering_line_irq;
			/* Check if LY==LYC STAT bit should be set */
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			else
			{
				LCDSTAT &= ~0x04;
			}
			/* Check for regular compensation of x-scroll register */
			state->lcd.scrollx_adjust = ( SCROLLX & 0x04 ) ? 4 : 0;
			/* Mode 2 last for approximately 80 clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(80), GB_LCD_STATE_LYXX_M3);
			break;
		case GB_LCD_STATE_LYXX_M3:		/* Switch to mode 3 */
			gb_select_sprites(state);
			state->lcd.sprite_cycles = sprite_cycles[ state->lcd.sprCount ];
			/* Set Mode 3 lcdstate */
			state->lcd.mode = 3;
			LCDSTAT = (LCDSTAT & 0xFC) | 0x03;
			state->lcd.vram_locked = LOCKED;
			state->lcd.pal_locked = LOCKED;
			/* Check for compensations of x-scroll register */
			/* Mode 3 lasts for approximately 172+cycles needed to handle sprites clock cycles */
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(168 + state->lcd.scrollx_adjust + state->lcd.sprite_cycles), GB_LCD_STATE_LYXX_PRE_M0);
			state->lcd.start_x = -1;
			break;
		case GB_LCD_STATE_LY9X_M1:		/* Switch to or stay in mode 1 */
			if ( CURLINE == 144 )
			{
				/* Trigger VBlank interrupt */
				cputag_set_input_line( machine, "maincpu", VBL_INT, ASSERT_LINE );
				/* Set VBlank lcdstate */
				state->lcd.mode = 1;
				LCDSTAT = (LCDSTAT & 0xFC) | 0x01;
				/* Trigger LCD interrupt if requested */
				if ( LCDSTAT & 0x10 )
				{
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
			/* Check if LY==LYC STAT bit should be set */
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			else
			{
				LCDSTAT &= ~0x04;
			}
			if ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(452), GB_LCD_STATE_LY9X_M1_INC);
			break;
		case GB_LCD_STATE_LY9X_M1_INC:		/* Increment scanline counter */
			gb_increment_scanline(state);
			state->lcd.delayed_line_irq = state->lcd.line_irq;
			state->lcd.triggering_line_irq = ( ( CMPLINE == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			if ( ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			if ( state->lcd.current_line == 153 )
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M1);
			}
			else
			{
				state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY9X_M1);
			}
			break;
		case GB_LCD_STATE_LY00_M1:		/* we stay in VBlank but current line counter should already be incremented */
			/* Check LY=LYC for line #153 */
			if ( state->lcd.delayed_line_irq )
			{
				if ( state->lcd.triggering_line_irq )
				{
					cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
			state->lcd.delayed_line_irq = state->lcd.delayed_line_irq | state->lcd.line_irq;
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			else
			{
				LCDSTAT &= ~0x04;
			}
			gb_increment_scanline(state);
			state->lcd.triggering_line_irq = ( ( CMPLINE == CURLINE ) && ( LCDSTAT & 0x40 ) ) ? 1 : 0;
			state->lcd.line_irq = 0;
			LCDSTAT &= 0xFB;
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M1_1);
			break;
		case GB_LCD_STATE_LY00_M1_1:
			if ( ! state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M1_2);
			break;
		case GB_LCD_STATE_LY00_M1_2:	/* Rest of line #0 during VBlank */
			if ( state->lcd.delayed_line_irq && state->lcd.triggering_line_irq )
			{
				state->lcd.line_irq = state->lcd.triggering_line_irq;
				cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			if ( CURLINE == CMPLINE )
			{
				LCDSTAT |= 0x04;
			}
			else
			{
				LCDSTAT &= ~0x04;
			}
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(444), GB_LCD_STATE_LY00_M0);
			break;
		case GB_LCD_STATE_LY00_M0:		/* The STAT register seems to go to 0 for about 4 cycles */
			/* Set Mode 0 lcdstat */
			state->lcd.mode = 0;
			state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(4), GB_LCD_STATE_LY00_M2);
			break;
		}
	}
	else
	{
		gb_increment_scanline(state);
		if ( state->lcd.current_line < 144 )
		{
			(*state->update_scanline)( machine );
		}
		state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(456));
	}
}

static void gb_lcd_switch_on( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	state->lcd.current_line = 0;
	state->lcd.previous_line = 153;
	state->lcd.window_lines_drawn = 0;
	state->lcd.line_irq = 0;
	state->lcd.delayed_line_irq = 0;
	state->lcd.mode = 0;
	state->lcd.oam_locked = LOCKED;	/* TODO: Investigate whether this OAM locking is correct. */
	/* Check for LY=LYC coincidence */
	if ( CURLINE == CMPLINE )
	{
		LCDSTAT |= 0x04;
		/* Generate lcd interrupt if requested */
		if ( LCDSTAT & 0x40 )
		{
			cputag_set_input_line( machine, "maincpu", LCD_INT, ASSERT_LINE );
		}
	}
	state->lcd.state = GB_LCD_STATE_LY00_M2;
	state->lcd.lcd_timer->adjust(machine->device<cpu_device>("maincpu")->cycles_to_attotime(80), GB_LCD_STATE_LYXX_M3);
}

READ8_HANDLER( gb_video_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	return state->lcd.gb_vid_regs[offset];
}

READ8_HANDLER( gb_vram_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	return ( state->lcd.vram_locked == LOCKED ) ? 0xFF : state->lcd.gb_vram_ptr[offset];
}

WRITE8_HANDLER( gb_vram_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	if ( state->lcd.vram_locked == LOCKED )
	{
		return;
	}
	state->lcd.gb_vram_ptr[offset] = data;
}

READ8_HANDLER( gb_oam_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	return ( state->lcd.oam_locked == LOCKED ) ? 0xFF : state->lcd.gb_oam->base()[offset];
}

WRITE8_HANDLER( gb_oam_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	if ( state->lcd.oam_locked == LOCKED || offset >= 0xa0 )
	{
		return;
	}
	state->lcd.gb_oam->base()[offset] = data;
}

WRITE8_HANDLER ( gb_video_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch (offset)
	{
	case 0x00:						/* LCDC - LCD Control */
		state->lcd.gb_chrgen = state->lcd.gb_vram->base() + ((data & 0x10) ? 0x0000 : 0x0800);
		state->lcd.gb_tile_no_mod = (data & 0x10) ? 0x00 : 0x80;
		state->lcd.gb_bgdtab = state->lcd.gb_vram->base() + ((data & 0x08) ? 0x1C00 : 0x1800 );
		state->lcd.gb_wndtab = state->lcd.gb_vram->base() + ((data & 0x40) ? 0x1C00 : 0x1800 );
		/* if LCD controller is switched off, set STAT and LY to 00 */
		if ( ! ( data & 0x80 ) )
		{
			LCDSTAT &= ~0x03;
			CURLINE = 0;
			state->lcd.oam_locked = UNLOCKED;
			state->lcd.vram_locked = UNLOCKED;
		}
		/* If LCD is being switched on */
		if ( !( LCDCONT & 0x80 ) && ( data & 0x80 ) )
		{
			gb_lcd_switch_on( space->machine );
		}
		break;
	case 0x01:						/* STAT - LCD Status */
		data = 0x80 | (data & 0x78) | (LCDSTAT & 0x07);
		/*
           Check for the STAT bug:
           Writing to STAT when the LCD controller is active causes a STAT
           interrupt to be triggered.
         */
		if ( LCDCONT & 0x80 )
		{
			/* Triggers seen so far:
               - 0x40 -> 0x00 - trigger
               - 0x00 -> 0x08 - trigger
               - 0x08 -> 0x00 - don't trigger
               - 0x00 -> 0x20 (mode 3) - trigger
               - 0x00 -> 0x60 (mode 2) - don't trigger
               - 0x20 -> 0x60 (mode 3) - trigger
               - 0x20 -> 0x40 (mode 3) - trigger
               - 0x40 -> 0x20 (mode 2) - don't trigger
               - 0x40 -> 0x08 (mode 0) - don't trigger
               - 0x00 -> 0x40 - trigger only if LY==LYC
               - 0x20 -> 0x00/0x08/0x10/0x20/0x40 (mode 2, after m2int) - don't trigger
               - 0x20 -> 0x00/0x08/0x10/0x20/0x40 (mode 3, after m2int) - don't trigger
            */
			if ( ! state->lcd.mode_irq && ( ( state->lcd.mode == 1 ) ||
				( ( LCDSTAT & 0x40 ) && ! ( data & 0x68 ) ) ||
				( ! ( LCDSTAT & 0x40 ) && ( data & 0x40 ) && ( LCDSTAT & 0x04 ) ) ||
				( ! ( LCDSTAT & 0x48 ) && ( data & 0x08 ) ) ||
				( ( LCDSTAT & 0x60 ) == 0x00 && ( data & 0x60 ) == 0x20 ) ||
				( ( LCDSTAT & 0x60 ) == 0x20 && ( data & 0x40 ) )
				) )
			{
					cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/*
               - 0x20 -> 0x08/0x18/0x28/0x48 (mode 0, after m2int) - trigger
               - 0x20 -> 0x00/0x10/0x20/0x40 (mode 0, after m2int) - trigger (stat bug)
               - 0x00 -> 0xXX (mode 0) - trigger stat bug
            */
			if ( state->lcd.mode_irq && state->lcd.mode == 0 )
			{
				cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
		}
		break;
	case 0x04:						/* LY - LCD Y-coordinate */
		return;
	case 0x05:						/* LYC */
		if ( CMPLINE != data )
		{
			if ( CURLINE == data )
			{
				if ( state->lcd.state != GB_LCD_STATE_LYXX_M0_INC && state->lcd.state != GB_LCD_STATE_LY9X_M1_INC )
				{
					LCDSTAT |= 0x04;
					/* Generate lcd interrupt if requested */
					if ( LCDSTAT & 0x40 )
					{
						cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
					}
				}
			}
			else
			{
				LCDSTAT &= 0xFB;
				state->lcd.triggering_line_irq = 0;
			}
		}
		break;
	case 0x06:						/* DMA - DMA Transfer and Start Address */
		{
			UINT8 *P = state->lcd.gb_oam->base();
			offset = (UINT16) data << 8;
			for (data = 0; data < 0xA0; data++)
				*P++ = space->read_byte(offset++);
		}
		return;
	case 0x07:						/* BGP - Background Palette */
		(*state->update_scanline)( space->machine );
		state->lcd.gb_bpal[0] = data & 0x3;
		state->lcd.gb_bpal[1] = (data & 0xC) >> 2;
		state->lcd.gb_bpal[2] = (data & 0x30) >> 4;
		state->lcd.gb_bpal[3] = (data & 0xC0) >> 6;
		break;
	case 0x08:						/* OBP0 - Object Palette 0 */
//      (*state->update_scanline)( machine );
		state->lcd.gb_spal0[0] = data & 0x3;
		state->lcd.gb_spal0[1] = (data & 0xC) >> 2;
		state->lcd.gb_spal0[2] = (data & 0x30) >> 4;
		state->lcd.gb_spal0[3] = (data & 0xC0) >> 6;
		break;
	case 0x09:						/* OBP1 - Object Palette 1 */
//      (*state->update_scanline)( machine );
		state->lcd.gb_spal1[0] = data & 0x3;
		state->lcd.gb_spal1[1] = (data & 0xC) >> 2;
		state->lcd.gb_spal1[2] = (data & 0x30) >> 4;
		state->lcd.gb_spal1[3] = (data & 0xC0) >> 6;
		break;
	case 0x02:						/* SCY - Scroll Y */
	case 0x03:						/* SCX - Scroll X */
		(*state->update_scanline)( space->machine );
	case 0x0A:						/* WY - Window Y position */
	case 0x0B:						/* WX - Window X position */
		break;
	default:						/* Unknown register, no change */
		return;
	}
	state->lcd.gb_vid_regs[ offset ] = data;
}

READ8_HANDLER( gbc_video_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch( offset )
	{
	case 0x11:	/* FF51 */
	case 0x12:	/* FF52 */
	case 0x13:	/* FF53 */
	case 0x14:	/* FF54 */
		return 0xFF;
	case 0x29:	/* FF69 */
	case 0x2B:	/* FF6B */
		if ( state->lcd.pal_locked == LOCKED )
		{
			return 0xFF;
		}
		break;
	}
	return state->lcd.gb_vid_regs[offset];
}

WRITE8_HANDLER ( gbc_video_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch( offset )
	{
	case 0x00:      /* LCDC - LCD Control */
		state->lcd.gb_chrgen = state->lcd.gb_vram->base() + ((data & 0x10) ? 0x0000 : 0x0800);
		state->lcd.gbc_chrgen = state->lcd.gb_vram->base() + ((data & 0x10) ? 0x2000 : 0x2800);
		state->lcd.gb_tile_no_mod = (data & 0x10) ? 0x00 : 0x80;
		state->lcd.gb_bgdtab = state->lcd.gb_vram->base() + ((data & 0x08) ? 0x1C00 : 0x1800);
		state->lcd.gbc_bgdtab = state->lcd.gb_vram->base() + ((data & 0x08) ? 0x3C00 : 0x3800);
		state->lcd.gb_wndtab = state->lcd.gb_vram->base() + ((data & 0x40) ? 0x1C00 : 0x1800);
		state->lcd.gbc_wndtab = state->lcd.gb_vram->base() + ((data & 0x40) ? 0x3C00 : 0x3800);
		/* if LCD controller is switched off, set STAT to 00 */
		if ( ! ( data & 0x80 ) )
		{
			LCDSTAT &= ~0x03;
			CURLINE = 0;
			state->lcd.oam_locked = UNLOCKED;
			state->lcd.vram_locked = UNLOCKED;
			state->lcd.pal_locked = UNLOCKED;
		}
		/* If LCD is being switched on */
		if ( !( LCDCONT & 0x80 ) && ( data & 0x80 ) )
		{
			gb_lcd_switch_on( space->machine );
		}
		break;
	case 0x01:      /* STAT - LCD Status */
		data = 0x80 | (data & 0x78) | (LCDSTAT & 0x07);
		if ( LCDCONT & 0x80 )
		{
			/*
               - 0x20 -> 0x08/0x18/0x28/0x48 (mode 0, after m2int) - trigger
            */
			if ( state->lcd.mode_irq && state->lcd.mode == 0 && ( LCDSTAT & 0x28 ) == 0x20 && ( data & 0x08 ) )
			{
				cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
			}
			/* Check if line irqs are being disabled */
			if ( ! ( data & 0x40 ) )
			{
				state->lcd.delayed_line_irq = 0;
			}
			/* Check if line irqs are being enabled */
			if ( ! ( LCDSTAT & 0x40 ) && ( data & 0x40 ) )
			{
				if ( CMPLINE == CURLINE )
				{
					state->lcd.line_irq = 1;
					cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
		}
		break;
	case 0x05:                      /* LYC */
		if ( CMPLINE != data )
		{
			if ( ( state->lcd.state != GB_LCD_STATE_LYXX_M0_PRE_INC && CURLINE == data ) ||
			     ( state->lcd.state == GB_LCD_STATE_LYXX_M0_INC && state->lcd.triggering_line_irq ) )
			{
				LCDSTAT |= 0x04;
				/* Generate lcd interrupt if requested */
				if ( LCDSTAT & 0x40 )
				{
					cputag_set_input_line( space->machine, "maincpu", LCD_INT, ASSERT_LINE );
				}
			}
			else
			{
				LCDSTAT &= 0xFB;
				state->lcd.triggering_line_irq = 0;
				state->lcd.cmp_line = data;
			}
		}
		break;
	case 0x07:      /* BGP - GB background palette */
		(*state->update_scanline)( space->machine );
		state->lcd.gb_bpal[0] = data & 0x3;
		state->lcd.gb_bpal[1] = (data & 0xC) >> 2;
		state->lcd.gb_bpal[2] = (data & 0x30) >> 4;
		state->lcd.gb_bpal[3] = (data & 0xC0) >> 6;
		break;
	case 0x08:      /* OBP0 - GB Object 0 palette */
		state->lcd.gb_spal0[0] = data & 0x3;
		state->lcd.gb_spal0[1] = (data & 0xC) >> 2;
		state->lcd.gb_spal0[2] = (data & 0x30) >> 4;
		state->lcd.gb_spal0[3] = (data & 0xC0) >> 6;
		break;
	case 0x09:      /* OBP1 - GB Object 1 palette */
		state->lcd.gb_spal1[0] = data & 0x3;
		state->lcd.gb_spal1[1] = (data & 0xC) >> 2;
		state->lcd.gb_spal1[2] = (data & 0x30) >> 4;
		state->lcd.gb_spal1[3] = (data & 0xC0) >> 6;
		break;
	case 0x0c:		/* Undocumented register involved in selecting gb/gbc mode */
		logerror( "Write to undocumented register: %X = %X\n", offset, data );
		break;
	case 0x0F:		/* VBK - VRAM bank select */
		state->lcd.gb_vram_ptr = state->lcd.gb_vram->base() + ( data & 0x01 ) * 0x2000;
		data |= 0xFE;
		break;
	case 0x11:      /* HDMA1 - HBL General DMA - Source High */
		break;
	case 0x12:      /* HDMA2 - HBL General DMA - Source Low */
		data &= 0xF0;
		break;
	case 0x13:      /* HDMA3 - HBL General DMA - Destination High */
		data &= 0x1F;
		break;
	case 0x14:      /* HDMA4 - HBL General DMA - Destination Low */
		data &= 0xF0;
		break;
	case 0x15:      /* HDMA5 - HBL General DMA - Mode, Length */
		if( !(data & 0x80) )
		{
			if( state->lcd.hdma_enabled )
			{
				state->lcd.hdma_enabled = 0;
				data = HDMA5 & 0x80;
			}
			else
			{
				/* General DMA */
				gbc_hdma( space->machine, ((data & 0x7F) + 1) * 0x10 );
//              cpunum_set_reg( 0, LR35902_DMA_CYCLES, 4 + ( ( ( data & 0x7F ) + 1 ) * 32 ) );
				data = 0xff;
			}
		}
		else
		{
			/* H-Blank DMA */
			state->lcd.hdma_enabled = 1;
			data &= 0x7f;
			state->lcd.gb_vid_regs[offset] = data;
			/* Check if HDMA should be immediately performed */
			if ( state->lcd.hdma_possible )
			{
				gbc_hdma( space->machine, 0x10 );
//              cpunum_set_reg( 0, LR35902_DMA_CYCLES, 36 );
				state->lcd.hdma_possible = 0;
			}
		}
		break;
	case 0x28:      /* BCPS - Background palette specification */
		GBCBCPS = data;
		if (data & 0x01)
			GBCBCPD = state->lcd.cgb_bpal[( data >> 1 ) & 0x1F] >> 8;
		else
			GBCBCPD = state->lcd.cgb_bpal[( data >> 1 ) & 0x1F] & 0xFF;
		break;
	case 0x29:      /* BCPD - background palette data */
		if ( state->lcd.pal_locked == LOCKED )
		{
			return;
		}
		GBCBCPD = data;
		if (GBCBCPS & 0x01)
			state->lcd.cgb_bpal[( GBCBCPS >> 1 ) & 0x1F] = ((data << 8) | (state->lcd.cgb_bpal[( GBCBCPS >> 1 ) & 0x1F] & 0xFF)) & 0x7FFF;
		else
			state->lcd.cgb_bpal[( GBCBCPS >> 1 ) & 0x1F] = ((state->lcd.cgb_bpal[( GBCBCPS >> 1 ) & 0x1F] & 0xFF00) | data) & 0x7FFF;
		if( GBCBCPS & 0x80 )
		{
			GBCBCPS++;
			GBCBCPS &= 0xBF;
		}
		break;
	case 0x2A:      /* OCPS - Object palette specification */
		GBCOCPS = data;
		if (data & 0x01)
			GBCOCPD = state->lcd.cgb_spal[( data >> 1 ) & 0x1F] >> 8;
		else
			GBCOCPD = state->lcd.cgb_spal[( data >> 1 ) & 0x1F] & 0xFF;
		break;
	case 0x2B:      /* OCPD - Object palette data */
		if ( state->lcd.pal_locked == LOCKED )
		{
			return;
		}
		GBCOCPD = data;
		if (GBCOCPS & 0x01)
			state->lcd.cgb_spal[( GBCOCPS >> 1 ) & 0x1F] = ((data << 8) | (state->lcd.cgb_spal[( GBCOCPS >> 1 ) & 0x1F] & 0xFF)) & 0x7FFF;
		else
			state->lcd.cgb_spal[( GBCOCPS >> 1 ) & 0x1F] = ((state->lcd.cgb_spal[( GBCOCPS >> 1 ) & 0x1F] & 0xFF00) | data) & 0x7FFF;
		if( GBCOCPS & 0x80 )
		{
			GBCOCPS++;
			GBCOCPS &= 0xBF;
		}
		break;
	/* Undocumented registers */
	case 0x2C:
		/* bit 0 can be read/written */
		logerror( "Write to undocumented register: %X = %X\n", offset, data );
		data = 0xFE | ( data & 0x01 );
		if ( data & 0x01 )
		{
			state->lcd.gbc_mode = 0;
		}
		break;
	case 0x32:
	case 0x33:
	case 0x34:
		/* whole byte can be read/written */
		logerror( "Write to undocumented register: %X = %X\n", offset, data );
		break;
	case 0x35:
		/* bit 4-6 can be read/written */
		logerror( "Write to undocumented register: %X = %X\n", offset, data );
		data = 0x8F | ( data & 0x70 );
		break;
	case 0x36:
	case 0x37:
		logerror( "Write to undocumented register: %X = %X\n", offset, data );
		return;
	default:
		/* we didn't handle the write, so pass it to the GB handler */
		gb_video_w( space, offset, data );
		return;
	}

	state->lcd.gb_vid_regs[offset] = data;
}


UINT8 *gb_get_vram_ptr(running_machine *machine)
{
	gb_state *state = machine->driver_data<gb_state>();
	return state->lcd.gb_vram_ptr;
}

