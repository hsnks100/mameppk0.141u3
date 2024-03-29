/***************************************************************************


    Funtech Super A'Can
    -------------------

    Preliminary driver by Angelo Salese
    Improvements by Harmony


*******************************************************************************

INFO:

    The system unit contains a reset button.

    Controllers:
    - 4 directional buttons
    - A, B, X, Y, buttons
    - Start, select buttons
    - L, R shoulder buttons

STATUS:

    The driver is begging for a re-write or at least a split into video/supracan.c.  It will happen eventually.

    Sound CPU comms and sound chip are completely unknown.

    There are 6 interrupt sources on the 6502 side, all of which use the IRQ line.
    The register at 0x411 is bitmapped to indicate what source(s) are active.
    In priority order from most to least important, they are:

    411 value  How acked                     Notes
    0x40       read reg 0x16 of sound chip   likely timer. snd regs 0x16/0x17 are time constant, write 0 to reg 0x9f to start
    0x04       read at 0x405                 latch 1?  0xcd is magic value
    0x08       read at 0x404                 latch 2?  0xcd is magic value
    0x10       read at 0x409                 unknown, dispatched but not used in startup 6502 code
    0x20       read at 0x40a                 possible periodic like vblank?
    0x80       read reg 0x14 of sound chip   depends on reg 0x14 of sound chip & 0x40: if not set writes 0x8f to reg 0x14,
                                             otherwise writes 0x4f to reg 0x14 and performs additional processing

    Known unemulated graphical effects and issues:
    - All: Sprite sizing is still imperfect.
    - All: Sprites need to be converted to use scanline rendering for proper clipping.
    - All: Improperly-emulated 1bpp ROZ mode, used by the Super A'Can BIOS logo.
    - All: Unimplemented ROZ scaling tables, used by the Super A'Can BIOS logo and Speedy Dragon intro, among others.
    - All: Priorities are largely unknown.
    - C.U.G.: Gameplay backgrounds are broken.
    - Sango Fighter: Possible missing masking on the upper edges of the screen during gameplay.
    - Sango Fighter: Raster effects off by 1 line
    - Sango Fighter: Specifies tiles out of range of video ram??
    - Speedy Dragon: Backgrounds are broken (wrong tile bank/region).
    - Super Taiwanese Baseball League: Does not boot, uses an unemulated DMA type
    - Super Taiwanese Baseball League: Missing window effect applied on tilemaps?
    - The Son of Evil: Many graphical issues.
    - Visible area, looks like it should be 224 pixels high at most, most games need 8 off the top and 8 off the bottom (or a global scroll)
      Sango looks like it needs 16 off the bottom instead
      Visible area is almost certainly 224 as Son of Evil has an explicit check in the vblank handler

    - All: are ALL the layers ROZ capable??

DEBUG TRICKS:

    baseball game debug trick:
    wpset e90020,1f,w
    do pc=5ac40
    ...
    do pc=5acd4
    wpclear
    bp 0269E4
    [ff7be4] <- 0x269ec
    bpclear

***************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "cpu/m6502/m6502.h"
#include "imagedev/cartslot.h"
#include "debugger.h"

#define SOUNDCPU_BOOT_HACK      (1)

#define DRAW_DEBUG_ROZ          (0)

#define DRAW_DEBUG_UNK_SPRITE   (0)

#define DEBUG_PRIORITY          (0)
#define DEBUG_PRIORITY_INDEX    (0) // 0-3

#define VERBOSE_LEVEL   (3)

#define ENABLE_VERBOSE_LOG (1)

typedef struct _acan_dma_regs_t acan_dma_regs_t;
struct _acan_dma_regs_t
{
	UINT32 source[2];
	UINT32 dest[2];
	UINT16 count[2];
	UINT16 control[2];
};

typedef struct _acan_sprdma_regs_t acan_sprdma_regs_t;
struct _acan_sprdma_regs_t
{
	UINT32 src;
	UINT16 src_inc;
	UINT32 dst;
	UINT16 dst_inc;
	UINT16 count;
	UINT16 control;
};

class supracan_state : public driver_device
{
public:
	supracan_state(running_machine &machine, const driver_device_config_base &config) : driver_device(machine, config)
	{
		m6502_reset = 0;
	}

	acan_dma_regs_t acan_dma_regs;
	acan_sprdma_regs_t acan_sprdma_regs;

	UINT16 m6502_reset;
	UINT8 *soundram;
	UINT8 soundlatch;
	UINT8 soundcpu_irq_src;
	UINT8 sound_irq_enable_reg;
	UINT8 sound_irq_source_reg;
	UINT8 sound_cpu_68k_irq_reg;

	emu_timer *video_timer;
	emu_timer *hbl_timer;
	emu_timer *line_on_timer;
	emu_timer *line_off_timer;
	UINT16 *vram;
	UINT16 *vram_swapped;
	UINT8 *vram_addr_swapped;

	UINT16 *pram;

	UINT16 sprite_count;
	UINT32 sprite_base_addr;
	UINT8 sprite_flags;

	UINT32 tilemap_base_addr[3];
	int tilemap_scrollx[3];
	int tilemap_scrolly[3];
	UINT16 video_flags;
	UINT16 tilemap_flags[3];
	UINT16 tilemap_mode[3];
	UINT16 irq_mask;
	UINT16 hbl_mask;

	UINT32 roz_base_addr;
	UINT16 roz_mode;
	UINT32 roz_scrollx;
	UINT32 roz_scrolly;
	UINT16 roz_tile_bank;
	UINT32 roz_unk_base0;
	UINT32 roz_unk_base1;
	UINT32 roz_unk_base2;
	UINT16 roz_coeffa;
	UINT16 roz_coeffb;
	UINT16 roz_coeffc;
	UINT16 roz_coeffd;
	INT32 roz_changed;
	INT32 roz_cx;
	INT32 roz_cy;
	UINT16 unk_1d0;

	UINT16 video_regs[256];

	bool hack_68k_to_6502_access;

	tilemap_t *tilemap_sizes[4][4];
	bitmap_t	 *sprite_final_bitmap;
};

static READ16_HANDLER( supracan_68k_soundram_r );
static WRITE16_HANDLER( supracan_68k_soundram_w );
static READ16_HANDLER( supracan_sound_r );
static WRITE16_HANDLER( supracan_sound_w );
static READ16_HANDLER( supracan_video_r );
static WRITE16_HANDLER( supracan_video_w );

#if ENABLE_VERBOSE_LOG
INLINE void verboselog(const char *tag, running_machine *machine, int n_level, const char *s_fmt, ...)
{
	if( VERBOSE_LEVEL >= n_level )
	{
		va_list v;
		char buf[ 32768 ];
		va_start( v, s_fmt );
		vsprintf( buf, s_fmt, v );
		va_end( v );
		logerror( "%06x: %s: %s", cpu_get_pc(machine->device(tag)), tag, buf );
	}
}

#else
#define verboselog(w,x,y,z,...)
#endif

static int supracan_tilemap_get_region(running_machine* machine, int layer)
{
	supracan_state *state = machine->driver_data<supracan_state>();

	// HACK!!!
	if (layer==2)
	{
		return 2;
	}


	if (layer==3)
	{
		// roz layer

		int gfx_mode = (state->roz_mode & 3);

		switch(gfx_mode)
		{
			case 0:	return 4;
			case 1: return 2;
			case 2: return 1;
			case 3: return 0;
		}
		return 1;
	}
	else
	{
		// normal layers
		int gfx_mode = (state->tilemap_mode[layer] & 0x7000) >> 12;

		switch(gfx_mode)
		{
			case 7: return 2;
			case 4: return 1;
			case 2: return 1;
			case 0: return 1;
		}

		return 1;
	}

}

static void supracan_tilemap_get_info_common(running_machine* machine, int layer, tile_data *tileinfo, int count)
{
	supracan_state *state = machine->driver_data<supracan_state>();

	UINT16* supracan_vram = state->vram;

	UINT32 base = (state->tilemap_base_addr[layer]);
	int gfx_mode = (state->tilemap_mode[layer] & 0x7000) >> 12;
	int region = supracan_tilemap_get_region(machine, layer);

	count += base;

	UINT16 tile_bank = 0;
	UINT16 palette_bank = 0;
	switch(gfx_mode)
	{
		case 7:
            tile_bank = 0x1c00;
            palette_bank = 0x00;
            break;

        case 4:
            tile_bank = 0x800;
            palette_bank = 0x00;
            break;

        case 2:
            tile_bank = 0x400;
            palette_bank = 0x00;
            break;

        case 0:
            tile_bank = 0;
            palette_bank = 0x00;
            break;

        default:
            verboselog("maincpu", machine, 0, "Unsupported tilemap mode: %d\n", (state->tilemap_mode[layer] & 0x7000) >> 12);
            break;
	}


	if(layer == 2)
	{
		tile_bank = 0x1000;
	}

	int tile = (supracan_vram[count] & 0x03ff) + tile_bank;
	int flipxy = (supracan_vram[count] & 0x0c00)>>10;
	int palette = ((supracan_vram[count] & 0xf000) >> 12) + palette_bank;

	SET_TILE_INFO(region, tile, palette, TILE_FLIPXY(flipxy));
}

// I wonder how different this really is.. my guess, not at all.
static void supracan_tilemap_get_info_roz(running_machine* machine, int layer, tile_data *tileinfo, int count)
{
	supracan_state *state = machine->driver_data<supracan_state>();

	UINT16* supracan_vram = state->vram;

	UINT32 base = state->roz_base_addr;


	int region = 1;
	UINT16 tile_bank = 0;
	UINT16 palette_bank = 0;

	region = supracan_tilemap_get_region(machine, layer);

    switch(state->roz_mode & 3) //FIXME: fix gfx bpp order
    {
        case 0:
			// hack: case for startup logo
			// this isn't understood properly, it's rendering a single 64x64 tile, which for convenience we've rearranged and decoded as 8x8 for the tilemaps
			{
				int tile = 0x880 + ((count & 7)*2);
			//  tile += (count & 0x070) >> 2;

				if (count & 0x20) tile ^= 1;
				tile |= (count & 0xc0)>>2;

				SET_TILE_INFO(region, tile, 0, 0);
				return;
			}


        case 1:
            tile_bank = (state->roz_tile_bank & 0xf000) >> 3;
            break;

        case 2:
            tile_bank = (state->roz_tile_bank & 0xf000) >> 3;
            break;

        case 3:
            tile_bank = (state->roz_tile_bank & 0xf000) >> 3;
            break;
    }

	count += base;

	int tile = (supracan_vram[count] & 0x03ff) + tile_bank;
	int flipxy = (supracan_vram[count] & 0x0c00)>>10;
	int palette = ((supracan_vram[count] & 0xf000) >> 12) + palette_bank;

	SET_TILE_INFO(region, tile, palette, TILE_FLIPXY(flipxy));
}



static TILE_GET_INFO( get_supracan_tilemap0_tile_info )
{

	supracan_tilemap_get_info_common(machine, 0, tileinfo, tile_index);
}

static TILE_GET_INFO( get_supracan_tilemap1_tile_info )
{
	supracan_tilemap_get_info_common(machine, 1, tileinfo, tile_index);
}

static TILE_GET_INFO( get_supracan_tilemap2_tile_info )
{

	supracan_tilemap_get_info_common(machine, 2, tileinfo, tile_index);
}

static TILE_GET_INFO( get_supracan_roz_tile_info )
{

	supracan_tilemap_get_info_roz(machine, 3, tileinfo, tile_index);
}


static VIDEO_START( supracan )
{
	supracan_state *state = machine->driver_data<supracan_state>();
	state->sprite_final_bitmap = auto_bitmap_alloc(machine, 1024, 1024, BITMAP_FORMAT_INDEXED16);

	state->vram = (UINT16*)machine->region("ram_gfx")->base();
	state->vram_swapped = (UINT16*)machine->region("ram_gfx2")->base();
	state->vram_addr_swapped = (UINT8*)machine->region("ram_gfx3")->base(); // hack for 1bpp layer at startup

	state->tilemap_sizes[0][0] = tilemap_create(machine, get_supracan_tilemap0_tile_info, tilemap_scan_rows, 8, 8, 32, 32);
	state->tilemap_sizes[0][1] = tilemap_create(machine, get_supracan_tilemap0_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	state->tilemap_sizes[0][2] = tilemap_create(machine, get_supracan_tilemap0_tile_info, tilemap_scan_rows, 8, 8, 128, 32);
	state->tilemap_sizes[0][3] = tilemap_create(machine, get_supracan_tilemap0_tile_info, tilemap_scan_rows, 8, 8, 64, 64);

	state->tilemap_sizes[1][0] = tilemap_create(machine, get_supracan_tilemap1_tile_info, tilemap_scan_rows, 8, 8, 32, 32);
	state->tilemap_sizes[1][1] = tilemap_create(machine, get_supracan_tilemap1_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	state->tilemap_sizes[1][2] = tilemap_create(machine, get_supracan_tilemap1_tile_info, tilemap_scan_rows, 8, 8, 128, 32);
	state->tilemap_sizes[1][3] = tilemap_create(machine, get_supracan_tilemap1_tile_info, tilemap_scan_rows, 8, 8, 64, 64);

	state->tilemap_sizes[2][0] = tilemap_create(machine, get_supracan_tilemap2_tile_info, tilemap_scan_rows, 8, 8, 32, 32);
	state->tilemap_sizes[2][1] = tilemap_create(machine, get_supracan_tilemap2_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	state->tilemap_sizes[2][2] = tilemap_create(machine, get_supracan_tilemap2_tile_info, tilemap_scan_rows, 8, 8, 128, 32);
	state->tilemap_sizes[2][3] = tilemap_create(machine, get_supracan_tilemap2_tile_info, tilemap_scan_rows, 8, 8, 64, 64);

	state->tilemap_sizes[3][0] = tilemap_create(machine, get_supracan_roz_tile_info, tilemap_scan_rows, 8, 8, 32, 32);
	state->tilemap_sizes[3][1] = tilemap_create(machine, get_supracan_roz_tile_info, tilemap_scan_rows, 8, 8, 64, 32);
	state->tilemap_sizes[3][2] = tilemap_create(machine, get_supracan_roz_tile_info, tilemap_scan_rows, 8, 8, 128, 32);
	state->tilemap_sizes[3][3] = tilemap_create(machine, get_supracan_roz_tile_info, tilemap_scan_rows, 8, 8, 64, 64);
}

static int get_tilemap_dimensions(running_machine* machine, int &xsize, int &ysize, int layer)
{
	supracan_state *state = (supracan_state *)machine->driver_data<supracan_state>();
	int select;

	xsize = 32;
	ysize = 32;

	if (layer==3) select = (state->roz_mode & 0x0f00);
	else select = state->tilemap_flags[layer] & 0x0f00;

	switch(select)
	{
		case 0x600:
			xsize = 64;
			ysize = 32;
			return 1;

		case 0xa00:
			xsize = 128;
			ysize = 32;
			return 2;

		case 0xc00:
			xsize = 64;
			ysize = 64;
			return 3;

		default:
			verboselog("maincpu", machine, 0, "Unsupported tilemap size for layer %d: %04x\n", layer, select);
			return 0;
	}
}




static void draw_sprites(running_machine *machine, bitmap_t *bitmap, const rectangle *cliprect)
{
	supracan_state *state = machine->driver_data<supracan_state>();
	UINT16 *supracan_vram = state->vram;

//      [0]
//      -e-- ---- ---- ---- sprite enable?
//      ---h hhh- ---- ---- Y size (not always right)
//      ---- ---y yyyy yyyy Y position
//      [1]
//      bbbb ---- ---- ---- Tile bank
//      ---- h--- ---- ---- Horizontal flip
//      ---- -v-- ---- ---- Vertical flip
//      ---- --mm ---- ---- Masking mode
//      ---- ---- ---- -www X size
//      [2]
//      zzzz ---- ---- ---- X scale
//      ---- ---x xxxx xxxx X position
//      [3]
//      d--- ---- ---- ---- Direct Sprite (use details from here, not looked up in vram)
//      -ooo oooo oooo oooo Sprite address

	UINT32 skip_count = 0;
    UINT32 start_word = (state->sprite_base_addr >> 1) + skip_count * 4;
    UINT32 end_word = start_word + (state->sprite_count - skip_count) * 4;
	int region = (state->sprite_flags & 1) ? 0 : 1; //8bpp : 4bpp

//  printf("frame\n");
	#define VRAM_MASK (0xffff)

   for(int i = start_word; i < end_word; i += 4)
	{
		int x = supracan_vram[i+2] & 0x01ff;
		int y = supracan_vram[i+0] & 0x01ff;

		int sprite_offset = (supracan_vram[i+3])<< 1;

		int bank = (supracan_vram[i+1] & 0xf000) >> 12;
        //int mask = (supracan_vram[i+1] & 0x0300) >> 8;
		int sprite_xflip = (supracan_vram[i+1] & 0x0800) >> 11;
		int sprite_yflip = (supracan_vram[i+1] & 0x0400) >> 10;
        //int xscale = (supracan_vram[i+2] & 0xf000) >> 12;
		const gfx_element *gfx = machine->gfx[region];




		// wraparound
		if (y>=0x180) y-=0x200;
		if (x>=0x180) x-=0x200;

		if((supracan_vram[i+0] & 0x4000))
		{

		#if 0
			printf("%d (unk %02x) (enable %02x) (unk Y2 %02x, %02x) (y pos %02x) (bank %01x) (flip %01x) (unknown %02x) (x size %02x) (xscale %01x) (unk %01x) (xpos %02x) (code %04x)\n", i,
				(supracan_vram[i+0] & 0x8000) >> 15,
				(supracan_vram[i+0] & 0x4000) >> 14,
				(supracan_vram[i+0] & 0x2000) >> 13,
				(supracan_vram[i+0] & 0x1e00) >> 8,
				(supracan_vram[i+0] & 0x01ff),
				(supracan_vram[i+1] & 0xf000) >> 12,
				(supracan_vram[i+1] & 0x0c00) >> 10,
				(supracan_vram[i+1] & 0x03f0) >> 4,
				(supracan_vram[i+1] & 0x000f),
				(supracan_vram[i+2] & 0xf000) >> 12,
				(supracan_vram[i+2] & 0x0e00) >> 8,
				(supracan_vram[i+2] & 0x01ff) >> 0,
				(supracan_vram[i+3] & 0xffff));
		#endif


			if (supracan_vram[i+3] &0x8000)
			{
				UINT16 data = supracan_vram[i+3];
				int tile = (bank * 0x200) + (data & 0x03ff);

				int palette = (data & 0xf000) >> 12; // this might not be correct, due to the &0x8000 condition above this would force all single tile sprites to be using palette >=0x8 only

				//printf("sprite data %04x %04x %04x %04x\n", supracan_vram[i+0] , supracan_vram[i+1] , supracan_vram[i+2] ,supracan_vram[i+3]  );

				drawgfx_transpen(bitmap,cliprect,gfx,tile,palette,sprite_xflip,sprite_yflip,
					x,
					y,
					0);

			}
			else
			{
				int xsize = 1 << (supracan_vram[i+1] & 7);
				int ysize = ((supracan_vram[i+0] & 0x1e00) >> 9) + 1;

				// I think the xsize must influence the ysize somehow, there are too many conflicting cases otherwise
				// there don't appear to be any special markers in the actual looked up tile data to indicate skip / end of list

				for(int ytile = 0; ytile < ysize; ytile++)
				{
					for(int xtile = 0; xtile< xsize; xtile++)
					{
						UINT16 data = supracan_vram[(sprite_offset+ytile*xsize+xtile)&VRAM_MASK];
						int tile = (bank * 0x200) + (data & 0x03ff);
						int palette = (data & 0xf000) >> 12;

						int xpos, ypos;

						if (!sprite_yflip)
						{
							ypos = y + ytile*8;
						}
						else
						{
							ypos = y - (ytile+1)*8;
							ypos += ysize*8;
						}

						if (!sprite_xflip)
						{
							xpos = x + xtile*8;
						}
						else
						{
							xpos = x - (xtile+1)*8;
							xpos += xsize*8;
						}

						int tile_xflip = sprite_xflip ^ ((data & 0x0800)>>11);
						int tile_yflip = sprite_yflip ^ ((data & 0x0400)>>10);

						drawgfx_transpen(bitmap,cliprect,gfx,tile,palette,tile_xflip,tile_yflip,xpos,ypos,0);
					}
				}
			}

#if 0
            if(xscale == 0) continue;
            UINT32 delta = (1 << 17) / xscale;
            for(int sy = 0; sy < ysize*8; sy++)
            {
                UINT16 *src = BITMAP_ADDR16(sprite_bitmap, sy, 0);
                UINT16 *dst = BITMAP_ADDR16(bitmap, y + sy, 0);
                UINT32 dx = x << 16;
                for(int sx = 0; sx < xsize*8; sx++)
                {
                    dst[dx >> 16] = src[sx];
                    dx += delta;
                }
            }
#endif

		}
	}
}



static void mark_active_tilemap_all_dirty(running_machine* machine, int layer)
{
	supracan_state *state = (supracan_state *)machine->driver_data<supracan_state>();
	int xsize = 0;
	int ysize = 0;

	int which_tilemap_size;

	which_tilemap_size = get_tilemap_dimensions(machine, xsize, ysize, layer);
//  for (int i=0;i<4;i++)
//      tilemap_mark_all_tiles_dirty(state->tilemap_sizes[layer][i]);
	tilemap_mark_all_tiles_dirty(state->tilemap_sizes[layer][which_tilemap_size]);
}



/* draws ROZ with linescroll OR columnscroll to 16-bit indexed bitmap */
static void supracan_suprnova_draw_roz(running_machine* machine, bitmap_t* bitmap, const rectangle *cliprect, tilemap_t *tmap, UINT32 startx, UINT32 starty, int incxx, int incxy, int incyx, int incyy, int wraparound/*, int columnscroll, UINT32* scrollram*/, int transmask)
{
	//bitmap_t *destbitmap = bitmap;
	bitmap_t *srcbitmap = tilemap_get_pixmap(tmap);
	//bitmap_t *srcbitmapflags = tilemap_get_flagsmap(tmap);
	const int xmask = srcbitmap->width-1;
	const int ymask = srcbitmap->height-1;
	const int widthshifted = srcbitmap->width << 16;
	const int heightshifted = srcbitmap->height << 16;
	UINT32 cx;
	UINT32 cy;
	int x;
	int sx;
	int sy;
	int ex;
	int ey;
	UINT16 *dest;
//  UINT8* destflags;
//  UINT8 *pri;
	//const UINT16 *src;
	//const UINT8 *maskptr;
	//int destadvance = destbitmap->bpp / 8;

	/* pre-advance based on the cliprect */
	startx += cliprect->min_x * incxx + cliprect->min_y * incyx;
	starty += cliprect->min_x * incxy + cliprect->min_y * incyy;

	/* extract start/end points */
	sx = cliprect->min_x;
	sy = cliprect->min_y;
	ex = cliprect->max_x;
	ey = cliprect->max_y;

	{
		/* loop over rows */
		while (sy <= ey)
		{

			/* initialize X counters */
			x = sx;
			cx = startx;
			cy = starty;

			/* get dest and priority pointers */
			dest = BITMAP_ADDR16( bitmap, sy, sx);
			//destflags = BITMAP_ADDR8( bitmapflags, sy, sx);

			/* loop over columns */
			while (x <= ex)
			{
				if ((wraparound) || (cx < widthshifted && cy < heightshifted)) // not sure how this will cope with no wraparound, but row/col scroll..
				{
					#if 0
					if (columnscroll)
					{
						int scroll = 0;//scrollram[(cx>>16)&0x3ff]);


						UINT16 data = BITMAP_ADDR16(srcbitmap,
						                        ((cy >> 16) - scroll) & ymask,
						                        (cx >> 16) & xmask)[0];

						if ((data & transmask)!=0)
							dest[0] = data;

						//destflags[0] = BITMAP_ADDR8(srcbitmapflags, ((cy >> 16) - scrollram[(cx>>16)&0x3ff]) & ymask, (cx >> 16) & xmask)[0];
					}
					else
					#endif
					{
						int scroll = 0;//scrollram[(cy>>16)&0x3ff]);
						UINT16 data =  BITMAP_ADDR16(srcbitmap,
						                       (cy >> 16) & ymask,
											   ((cx >> 16) - scroll) & xmask)[0];


						if ((data & transmask)!=0)
							dest[0] = data;

						//destflags[0] = BITMAP_ADDR8(srcbitmapflags, (cy >> 16) & ymask, ((cx >> 16) - scrollram[(cy>>16)&0x3ff]) & xmask)[0];
					}
				}

				/* advance in X */
				cx += incxx;
				cy += incxy;
				x++;
				dest++;
//              destflags++;
//              pri++;
			}

			/* advance in Y */
			startx += incyx;
			starty += incyy;
			sy++;
		}
	}
}


// VIDEO FLAGS                  ROZ MODE            TILEMAP FLAGS
//
//  Bit                         Bit                 Bit
// 15-9: Unknown                15-13: Priority?    15-13: Priority?
//    8: X ht. (256/320)        12: Unknown         12: Unknown
//    7: Tilemap 0 enable       11-8: Dims          11-8: Dims
//    6: Tilemap 1 enable       7-6: Unknown        7-6: Unknown
//    5: Tilemap 2 enable?      5: Wrap             5: Wrap
//    3: Sprite enable          4-2: Unknown        4-2: Mosaic
//    2: ROZ enable             1-0: Bit Depth      1-0: Bit Depth
//  1-0: Unknown

//                      Video Flags                 ROZ Mode                    Tilemap 0   Tilemap 1   Tilemap 2   VF Unk0
// A'Can logo:          120e: 0001 0010 0000 1110   4020: 0100 0000 0010 0000   4620        ----        ----        0x09
// Boomzoo Intro:       9a82: 1001 1010 1000 0010   0402: 0000 0100 0000 0010   6400        6400        4400        0x4d
// Boomzoo Title:       9acc: 1001 1010 1100 1100   0402: 0000 0100 0000 0010   6400        6400        4400        0x4d
// C.U.G. Intro:        11c8: 0001 0001 1100 1000   0402: 0000 0100 0000 0010   2400        4400        6400        0x08
// C.U.G. Title:        11cc: 0001 0001 1100 1100   0602: 0000 0110 0000 0010   2600        4600        ----        0x08
// Speedy Dragon Logo:  0388: 0000 0011 1000 1000   4020: 0100 0000 0010 0000   6c20        6c20        2600        0x01
// Speedy Dragon Title: 038c: 0000 0011 1000 1100   2603: 0010 0110 0000 0011   6c20        2c20        2600        0x01
// Sango Fighter Intro: 03c8: 0000 0011 1100 1000   ----: ---- ---- ---- ----   6c20        4620        ----        0x01
// Sango Fighter Game:  03ce: 0000 0011 1100 1110   0622: 0000 0110 0010 0010   2620        4620        ----        0x01

static SCREEN_UPDATE( supracan )
{
	supracan_state *state = (supracan_state *)screen->machine->driver_data<supracan_state>();



	// treat the sprites as frame-buffered and only update the buffer when drawing scanline 0 - this might not be true!

	if (0)
	{
		if (cliprect->min_y == 0x00)
		{
			const rectangle &visarea = screen->visible_area();

			bitmap_fill(state->sprite_final_bitmap, &visarea, 0x00);
			bitmap_fill(bitmap, &visarea, 0x80);

			draw_sprites(screen->machine, state->sprite_final_bitmap, &visarea);


		}
	}
	else
	{

		bitmap_fill(state->sprite_final_bitmap, cliprect, 0x00);
		bitmap_fill(bitmap, cliprect, 0x80);

		draw_sprites(screen->machine, state->sprite_final_bitmap, cliprect);


	}



	// mix screen
	int xsize = 0, ysize = 0;
	bitmap_t *src_bitmap = 0;
	int tilemap_num;
	int which_tilemap_size;
	int priority = 0;


	for (int pri=7;pri>=0;pri--)
	{

		for (int layer = 3; layer >=0; layer--)
		{
		//  popmessage("%04x\n",state->video_flags);
			int enabled = 0;

			if(state->video_flags & 0x04)
				if (layer==3) enabled = 1;

			if(state->video_flags & 0x80)
				if (layer==0) enabled = 1;

			if(state->video_flags & 0x40)
				if (layer==1) enabled = 1;

			if(state->video_flags & 0x20)
				if (layer==2) enabled = 1;


			if (layer==3) priority = ((state->roz_mode >> 13) & 7); // roz case
			else priority = ((state->tilemap_flags[layer] >> 13) & 7); // normal cases


			if (priority==pri)
			{
				tilemap_num = layer;
				which_tilemap_size = get_tilemap_dimensions(screen->machine, xsize, ysize, layer);
				src_bitmap = tilemap_get_pixmap(state->tilemap_sizes[layer][which_tilemap_size]);
				int gfx_region = supracan_tilemap_get_region(screen->machine, layer);
				int transmask = 0xff;

				switch (gfx_region)
				{
					case 0:transmask = 0xff; break;
					case 1:transmask = 0x0f; break;
					case 2:transmask = 0x03; break;
					case 3:transmask = 0x01; break;
					case 4:transmask = 0x01; break;
				}

				if (enabled)
				{
					if (layer != 3) // standard layers, NOT roz
					{

						int wrap = (state->tilemap_flags[layer] & 0x20);

						int scrollx = state->tilemap_scrollx[layer];
						int scrolly = state->tilemap_scrolly[layer];

						if (scrollx&0x8000) scrollx-= 0x10000;
						if (scrolly&0x8000) scrolly-= 0x10000;

						int mosaic_count = (state->tilemap_flags[layer] & 0x001c) >> 2;
						int mosaic_mask = 0xffffffff << mosaic_count;

						int y,x;
						// yes, it will draw a single line if you specify a cliprect as such (partial updates...)

						for (y=cliprect->min_y;y<=cliprect->max_y;y++)
						{
							// these will have to change to ADDR32 etc. once alpha blending is supported
							UINT16* screen = BITMAP_ADDR16(bitmap, y, 0);

							int actualy = y&mosaic_mask;

							int realy = actualy+scrolly;

							if (!wrap)
								if (scrolly+y < 0 || scrolly+y > ((ysize*8)-1))
									continue;


							UINT16* src = BITMAP_ADDR16(src_bitmap, (realy)&((ysize*8)-1), 0);

							for (x=cliprect->min_x;x<=cliprect->max_x;x++)
							{
								int actualx = x & mosaic_mask;
								int realx = actualx+scrollx;

								if (!wrap)
									if (scrollx+x < 0 || scrollx+x > ((xsize*8)-1))
										continue;

								UINT16 srcpix = src[(realx)&((xsize*8)-1)];

								if ((srcpix & transmask) != 0)
									screen[x] = srcpix;
							}
						}
					}
					else
					{
						int wrap = state->roz_mode & 0x20;

						int incxx = (state->roz_coeffa);
						int incyy = (state->roz_coeffd);

						int incxy = (state->roz_coeffc);
						int incyx = (state->roz_coeffb);

						int scrollx = (state->roz_scrollx);
						int scrolly = (state->roz_scrolly);






						if (incyx & 0x8000) incyx -= 0x10000;
						if (incxy & 0x8000) incxy -= 0x10000;

						if (incyy & 0x8000) incyy -= 0x10000;
						if (incxx & 0x8000) incxx -= 0x10000;

						//popmessage("%04x %04x\n",state->video_flags, state->roz_mode);

						// roz mode..
						//4020 = enabled speedyd
						//6c22 = enabled speedyd
						//2c22 = enabled speedyd
						//4622 = disabled jttlaugh
						//2602 = disabled monopoly
						//0402 = disabled (sango title)
						// or is it always enabled, and only corrupt because we don't clear ram properly?
						// (probably not this register?)

						if (!(state->roz_mode & 0x0200) && (state->roz_mode&0xf000) ) // HACK - Not Trusted, Acan Logo, Speedy Dragon Intro ,Speed Dragon Bonus stage need it.  Monopoly and JTT *don't* causes graphical issues
						{
							// NOT accurate, causes issues when the attract mode loops and the logo is shown the 2nd time in some games - investigate
							for (int y=cliprect->min_y;y<=cliprect->max_y;y++)
							{
								rectangle clip;
								clip.min_x = cliprect->min_x;
								clip.max_x = cliprect->max_x;

								clip.min_y = clip.max_y = y;

								scrollx = (state->roz_scrollx);
								scrolly = (state->roz_scrolly);
								incxx = (state->roz_coeffa);

								incxx += state->vram[state->roz_unk_base0/2 + y];

								scrollx += state->vram[state->roz_unk_base1/2 + y*2] << 16;
								scrollx += state->vram[state->roz_unk_base1/2 + y*2 + 1];

								scrolly += state->vram[state->roz_unk_base2/2 + y*2] << 16;
								scrolly += state->vram[state->roz_unk_base2/2 + y*2 + 1];

								if (incxx & 0x8000) incxx -= 0x10000;


								if (state->vram[state->roz_unk_base0/2 + y]) // incxx = 0, no draw?
									supracan_suprnova_draw_roz(screen->machine, bitmap, &clip, state->tilemap_sizes[layer][which_tilemap_size], scrollx<<8, scrolly<<8, incxx<<8, incxy<<8, incyx<<8, incyy<<8, wrap, transmask);
							}
						}
						else
						{
							supracan_suprnova_draw_roz(screen->machine, bitmap, cliprect, state->tilemap_sizes[layer][which_tilemap_size], scrollx<<8, scrolly<<8, incxx<<8, incxy<<8, incyx<<8, incyy<<8, wrap, transmask);
						}
					}
				}
			}
		}
	}


	// just draw the sprites on top for now
	if(state->video_flags & 0x08)
	{
		for (int y=cliprect->min_y;y<=cliprect->max_y;y++)
		{
			UINT16* src = BITMAP_ADDR16( state->sprite_final_bitmap, y, 0);
			UINT16* dst = BITMAP_ADDR16( bitmap, y, 0);

			for (int x=cliprect->min_x;x<=cliprect->max_x;x++)
			{
				UINT16 dat = src[x];
				if (dat) dst[x] = dat;
			}
		}
	}

	return 0;
}


static WRITE16_HANDLER( supracan_dma_w )
{
	supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	acan_dma_regs_t *acan_dma_regs = &state->acan_dma_regs;
	int ch = (offset < 0x10/2) ? 0 : 1;

	switch(offset)
	{
		case 0x00/2: // Source address MSW
		case 0x10/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: source msw %d: %04x\n", ch, data);
			acan_dma_regs->source[ch] &= 0x0000ffff;
			acan_dma_regs->source[ch] |= data << 16;
			break;
		case 0x02/2: // Source address LSW
		case 0x12/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: source lsw %d: %04x\n", ch, data);
			acan_dma_regs->source[ch] &= 0xffff0000;
			acan_dma_regs->source[ch] |= data;
			break;
		case 0x04/2: // Destination address MSW
		case 0x14/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: dest msw %d: %04x\n", ch, data);
			acan_dma_regs->dest[ch] &= 0x0000ffff;
			acan_dma_regs->dest[ch] |= data << 16;
			break;
		case 0x06/2: // Destination address LSW
		case 0x16/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: dest lsw %d: %04x\n", ch, data);
			acan_dma_regs->dest[ch] &= 0xffff0000;
			acan_dma_regs->dest[ch] |= data;
			break;
		case 0x08/2: // Byte count
		case 0x18/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: count %d: %04x\n", ch, data);
			acan_dma_regs->count[ch] = data;
			break;
		case 0x0a/2: // Control
		case 0x1a/2:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: control %d: %04x\n", ch, data);
			if(data & 0x8800)
			{
//              if(data & 0x2000)
//                  acan_dma_regs->source-=2;
				logerror("supracan_dma_w: Kicking off a DMA from %08x to %08x, %d bytes (%04x)\n", acan_dma_regs->source[ch], acan_dma_regs->dest[ch], acan_dma_regs->count[ch] + 1, data);

				for(int i = 0; i <= acan_dma_regs->count[ch]; i++)
				{
					if(data & 0x1000)
					{
						space->write_word(acan_dma_regs->dest[ch], space->read_word(acan_dma_regs->source[ch]));
						acan_dma_regs->dest[ch]+=2;
						acan_dma_regs->source[ch]+=2;
						if(data & 0x0100)
							if((acan_dma_regs->dest[ch] & 0xf) == 0)
								acan_dma_regs->dest[ch]-=0x10;
					}
					else
					{
						space->write_byte(acan_dma_regs->dest[ch], space->read_byte(acan_dma_regs->source[ch]));
						acan_dma_regs->dest[ch]++;
						acan_dma_regs->source[ch]++;
					}
				}
			}
			else if(data != 0x0000) // fake DMA, used by C.U.G.
			{
				verboselog("maincpu", space->machine, 0, "supracan_dma_w: Unknown DMA kickoff value of %04x (other regs %08x, %08x, %d)\n", data, acan_dma_regs->source[ch], acan_dma_regs->dest[ch], acan_dma_regs->count[ch] + 1);
				fatalerror("supracan_dma_w: Unknown DMA kickoff value of %04x (other regs %08x, %08x, %d)",data, acan_dma_regs->source[ch], acan_dma_regs->dest[ch], acan_dma_regs->count[ch] + 1);
			}
			break;
        default:
            verboselog("maincpu", space->machine, 0, "supracan_dma_w: Unknown register: %08x = %04x & %04x\n", 0xe90020 + (offset << 1), data, mem_mask);
            break;
	}
}

#if 0
static WRITE16_HANDLER( supracan_pram_w )
{
	supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	state->pram[offset] &= ~mem_mask;
	state->pram[offset] |= data & mem_mask;
}
#endif

// swap address around so that 64x64 tile can be decoded as 8x8 tiles..
static void write_swapped_byte(running_machine* machine, int offset, UINT8 byte )
{
	supracan_state *state = (supracan_state *)machine->driver_data<supracan_state>();
	int swapped_offset = BITSWAP32(offset, 31,30,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,7,2,1,0,6,5,4,3);

	state->vram_addr_swapped[swapped_offset] = byte;
}


static READ16_HANDLER( supracan_vram_r )
{
	supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	return state->vram[offset];
}


static WRITE16_HANDLER( supracan_vram_w )
{
	supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	COMBINE_DATA(&state->vram[offset]);

	// store a byteswapped vesrion for easier gfx-decode
	data = state->vram[offset];
	data = ((data & 0x00ff)<<8) | ((data & 0xff00)>>8);
	state->vram_swapped[offset] = data;

	// hack for 1bpp layer at startup
	write_swapped_byte(space->machine, offset*2+1, (data & 0xff00)>>8);
	write_swapped_byte(space->machine, offset*2, (data & 0x00ff));

	// mark tiles of each depth as dirty
	gfx_element_mark_dirty(space->machine->gfx[0], (offset*2)/(64));
	gfx_element_mark_dirty(space->machine->gfx[1], (offset*2)/(32));
	gfx_element_mark_dirty(space->machine->gfx[2], (offset*2)/(16));
	gfx_element_mark_dirty(space->machine->gfx[3], (offset*2)/(512));
	gfx_element_mark_dirty(space->machine->gfx[4], (offset*2)/(8));

}


static ADDRESS_MAP_START( supracan_mem, ADDRESS_SPACE_PROGRAM, 16 )
	AM_RANGE( 0x000000, 0x3fffff ) AM_ROM AM_REGION( "cart", 0 )
	AM_RANGE( 0xe80200, 0xe80201 ) AM_READ_PORT("P1")
	AM_RANGE( 0xe80202, 0xe80203 ) AM_READ_PORT("P2")
	AM_RANGE( 0xe80208, 0xe80209 ) AM_READ_PORT("P3")
	AM_RANGE( 0xe8020c, 0xe8020d ) AM_READ_PORT("P4")
	AM_RANGE( 0xe80000, 0xe8ffff ) AM_READWRITE( supracan_68k_soundram_r, supracan_68k_soundram_w )
	AM_RANGE( 0xe90000, 0xe9001f ) AM_READWRITE( supracan_sound_r, supracan_sound_w )
	AM_RANGE( 0xe90020, 0xe9003f ) AM_WRITE( supracan_dma_w )
	AM_RANGE( 0xf00000, 0xf001ff ) AM_READWRITE( supracan_video_r, supracan_video_w )
	AM_RANGE( 0xf00200, 0xf003ff ) AM_RAM_WRITE(paletteram16_xBBBBBGGGGGRRRRR_word_w) AM_BASE_GENERIC(paletteram)
	AM_RANGE( 0xf40000, 0xf5ffff ) AM_READWRITE(supracan_vram_r, supracan_vram_w)
	AM_RANGE( 0xfc0000, 0xfdffff ) AM_MIRROR(0x30000) AM_RAM /* System work ram */
ADDRESS_MAP_END

static READ8_HANDLER( supracan_6502_soundmem_r )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
    UINT8 data = state->soundram[offset];

    switch(offset)
    {
#if SOUNDCPU_BOOT_HACK
        case 0x300: // HACK to make games think the sound CPU is always reporting 'OK'.
            return 0xff;
#endif

        case 0x410: // Sound IRQ enable
            data = state->sound_irq_enable_reg;
            if(!space->debugger_access()) verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_r: IRQ enable: %04x\n", data);
            if(!space->debugger_access())
            {
                if(state->sound_irq_enable_reg & state->sound_irq_source_reg)
                {
                    cpu_set_input_line(space->machine->device("soundcpu"), 0, ASSERT_LINE);
                }
                else
                {
                    cpu_set_input_line(space->machine->device("soundcpu"), 0, CLEAR_LINE);
                }
            }
            break;
        case 0x411: // Sound IRQ source
            data = state->sound_irq_source_reg;
            state->sound_irq_source_reg = 0;
            if(!space->debugger_access()) verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 3, "supracan_soundreg_r: IRQ source: %04x\n", data);
            if(!space->debugger_access())
            {
                cpu_set_input_line(space->machine->device("soundcpu"), 0, CLEAR_LINE);
            }
            break;
        case 0x420:
            if(!space->debugger_access()) verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 3, "supracan_soundreg_r: Sound hardware status? (not yet implemented): %02x\n", 0);
            break;
        case 0x422:
            if(!space->debugger_access()) verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 3, "supracan_soundreg_r: Sound hardware data? (not yet implemented): %02x\n", 0);
            break;
        case 0x404:
        case 0x405:
        case 0x409:
        case 0x414:
        case 0x416:
            // Intentional fall-through
        default:
            if(offset >= 0x300 && offset < 0x500)
            {
                if(!space->debugger_access()) verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_r: Unknown register %04x\n", offset);
            }
            break;
    }

    return data;
}

static WRITE8_HANDLER( supracan_6502_soundmem_w )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();

    switch(offset)
    {
        case 0x407:
            if(state->sound_cpu_68k_irq_reg &~ data)
            {
                verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_w: sound_cpu_68k_irq_reg: %04x: Triggering M68k IRQ\n", data);
                cpu_set_input_line(space->machine->device("maincpu"), 7, HOLD_LINE);
            }
            else
            {
                verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_w: sound_cpu_68k_irq_reg: %04x\n", data);
            }
            state->sound_cpu_68k_irq_reg = data;
            break;
        case 0x410:
            state->sound_irq_enable_reg = data;
            verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_w: IRQ enable: %02x\n", data);
            break;
        case 0x420:
            verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 3, "supracan_soundreg_w: Sound hardware reg data? (not yet implemented): %02x\n", data);
            break;
        case 0x422:
            verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 3, "supracan_soundreg_w: Sound hardware reg addr? (not yet implemented): %02x\n", data);
            break;
        default:
            if(offset >= 0x300 && offset < 0x500)
            {
                verboselog(state->hack_68k_to_6502_access ? "maincpu" : "soundcpu", space->machine, 0, "supracan_soundreg_w: Unknown register %04x = %02x\n", offset, data);
            }
            state->soundram[offset] = data;
            break;
    }
}

static ADDRESS_MAP_START( supracan_sound_mem, ADDRESS_SPACE_PROGRAM, 8 )
    AM_RANGE( 0x0000, 0xffff ) AM_READWRITE(supracan_6502_soundmem_r, supracan_6502_soundmem_w) AM_BASE_MEMBER(supracan_state, soundram)
ADDRESS_MAP_END

static INPUT_PORTS_START( supracan )
	PORT_START("P1")
	PORT_DIPNAME( 0x01, 0x00, "SYSTEM" )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x02, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x04, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON6 ) PORT_PLAYER(1) PORT_NAME("P1 Button R")
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON5 ) PORT_PLAYER(1) PORT_NAME("P1 Button L")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON4 ) PORT_PLAYER(1) PORT_NAME("P1 Button Y")
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(1) PORT_NAME("P1 Button X")
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(1) PORT_NAME("P1 Joypad Right")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_PLAYER(1) PORT_NAME("P1 Joypad Left")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_PLAYER(1) PORT_NAME("P1 Joypad Down")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_PLAYER(1) PORT_NAME("P1 Joypad Up")
	PORT_DIPNAME( 0x1000, 0x0000, "SYSTEM" )
	PORT_DIPSETTING(    0x0000, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x1000, DEF_STR( On ) )
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_START1 )
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_PLAYER(1) PORT_NAME("P1 Button B")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(1) PORT_NAME("P1 Button A")

	PORT_START("P2")
	PORT_DIPNAME( 0x01, 0x00, "SYSTEM" )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x02, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x04, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON6 ) PORT_PLAYER(2) PORT_NAME("P2 Button R")
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON5 ) PORT_PLAYER(2) PORT_NAME("P2 Button L")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON4 ) PORT_PLAYER(2) PORT_NAME("P2 Button Y")
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(2) PORT_NAME("P2 Button X")
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(2) PORT_NAME("P2 Joypad Right")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_PLAYER(2) PORT_NAME("P2 Joypad Left")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_PLAYER(2) PORT_NAME("P2 Joypad Down")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_PLAYER(2) PORT_NAME("P2 Joypad Up")
	PORT_DIPNAME( 0x1000, 0x0000, "SYSTEM" )
	PORT_DIPSETTING(    0x0000, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x1000, DEF_STR( On ) )
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_START2 )
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_PLAYER(2) PORT_NAME("P2 Button B")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(2) PORT_NAME("P2 Button A")

	PORT_START("P3")
	PORT_DIPNAME( 0x01, 0x00, "SYSTEM" )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x02, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x04, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON6 ) PORT_PLAYER(3) PORT_NAME("P3 Button R")
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON5 ) PORT_PLAYER(3) PORT_NAME("P3 Button L")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON4 ) PORT_PLAYER(3) PORT_NAME("P3 Button Y")
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(3) PORT_NAME("P3 Button X")
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(3) PORT_NAME("P3 Joypad Right")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_PLAYER(3) PORT_NAME("P3 Joypad Left")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_PLAYER(3) PORT_NAME("P3 Joypad Down")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_PLAYER(3) PORT_NAME("P3 Joypad Up")
	PORT_DIPNAME( 0x1000, 0x0000, "SYSTEM" )
	PORT_DIPSETTING(    0x0000, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x1000, DEF_STR( On ) )
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_START3 )
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_PLAYER(3) PORT_NAME("P3 Button B")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(3) PORT_NAME("P3 Button A")

	PORT_START("P4")
	PORT_DIPNAME( 0x01, 0x00, "SYSTEM" )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x01, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x02, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x04, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x00, DEF_STR( Unknown ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x08, DEF_STR( On ) )
	PORT_BIT(0x0010, IP_ACTIVE_HIGH, IPT_BUTTON6 ) PORT_PLAYER(4) PORT_NAME("P4 Button R")
	PORT_BIT(0x0020, IP_ACTIVE_HIGH, IPT_BUTTON5 ) PORT_PLAYER(4) PORT_NAME("P4 Button L")
	PORT_BIT(0x0040, IP_ACTIVE_HIGH, IPT_BUTTON4 ) PORT_PLAYER(4) PORT_NAME("P4 Button Y")
	PORT_BIT(0x0080, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_PLAYER(4) PORT_NAME("P4 Button X")
	PORT_BIT(0x0100, IP_ACTIVE_HIGH, IPT_JOYSTICK_RIGHT ) PORT_PLAYER(4) PORT_NAME("P4 Joypad Right")
	PORT_BIT(0x0200, IP_ACTIVE_HIGH, IPT_JOYSTICK_LEFT ) PORT_PLAYER(4) PORT_NAME("P4 Joypad Left")
	PORT_BIT(0x0400, IP_ACTIVE_HIGH, IPT_JOYSTICK_DOWN ) PORT_PLAYER(4) PORT_NAME("P4 Joypad Down")
	PORT_BIT(0x0800, IP_ACTIVE_HIGH, IPT_JOYSTICK_UP ) PORT_PLAYER(4) PORT_NAME("P4 Joypad Up")
	PORT_DIPNAME( 0x1000, 0x0000, "SYSTEM" )
	PORT_DIPSETTING(    0x0000, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x1000, DEF_STR( On ) )
	PORT_BIT(0x2000, IP_ACTIVE_HIGH, IPT_START2 )
	PORT_BIT(0x4000, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_PLAYER(4) PORT_NAME("P4 Button B")
	PORT_BIT(0x8000, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_PLAYER(4) PORT_NAME("P4 Button A")
INPUT_PORTS_END

static PALETTE_INIT( supracan )
{
	// Used for debugging purposes for now
	//#if 0
	int i, r, g, b;

	for( i = 0; i < 32768; i++ )
	{
		r = (i & 0x1f) << 3;
		g = ((i >> 5) & 0x1f) << 3;
		b = ((i >> 10) & 0x1f) << 3;
		palette_set_color_rgb( machine, i, r, g, b );
	}
	//#endif
}

static WRITE16_HANDLER( supracan_68k_soundram_w )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();

    state->soundram[offset*2 + 1] = data & 0xff;
    state->soundram[offset*2 + 0] = data >> 8;

    if(offset*2 < 0x500 && offset*2 >= 0x300)
    {
        if(mem_mask & 0xff00)
        {
            state->hack_68k_to_6502_access = true;
            supracan_6502_soundmem_w(space, offset*2, data >> 8);
            state->hack_68k_to_6502_access = false;
        }
        if(mem_mask & 0x00ff)
        {
            state->hack_68k_to_6502_access = true;
            supracan_6502_soundmem_w(space, offset*2 + 1, data & 0xff);
            state->hack_68k_to_6502_access = false;
        }
    }
}

static READ16_HANDLER( supracan_68k_soundram_r )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();

    UINT16 val = state->soundram[offset*2 + 0] << 8;
    val |= state->soundram[offset*2 + 1];

    if(offset*2 >= 0x300 && offset*2 < 0x500)
    {
        val = 0;
        if(mem_mask & 0xff00)
        {
            state->hack_68k_to_6502_access = true;
            val |= supracan_6502_soundmem_r(space, offset*2) << 8;
            state->hack_68k_to_6502_access = false;
        }
        if(mem_mask & 0x00ff)
        {
            state->hack_68k_to_6502_access = true;
            val |= supracan_6502_soundmem_r(space, offset*2 + 1);
            state->hack_68k_to_6502_access = false;
        }
    }

    return val;
}

static READ16_HANDLER( supracan_sound_r )
{
    //supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
    UINT16 data = 0;

    switch( offset )
    {
        default:
            verboselog("maincpu", space->machine, 0, "supracan_sound_r: Unknown register: (%08x) & %04x\n", 0xe90000 + (offset << 1), mem_mask);
            break;
    }

    return data;
}

static WRITE16_HANDLER( supracan_sound_w )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();

	switch ( offset )
	{
        case 0x000a/2:  /* Sound cpu IRQ request. */
            cpu_set_input_line(space->machine->device("soundcpu"), 0, ASSERT_LINE);
            break;
		case 0x001c/2:	/* Sound cpu control. Bit 0 tied to sound cpu RESET line */
			if(data & 0x01)
			{
				if(!state->m6502_reset)
				{
					/* Reset and enable the sound cpu */
                    #if !(SOUNDCPU_BOOT_HACK)
					cputag_set_input_line(space->machine, "soundcpu", INPUT_LINE_HALT, CLEAR_LINE);
					space->machine->device("soundcpu")->reset();
                    #endif
				}
                state->m6502_reset = data & 0x01;
			}
			else
			{
				/* Halt the sound cpu */
				cputag_set_input_line(space->machine, "soundcpu", INPUT_LINE_HALT, ASSERT_LINE);
			}
            verboselog("maincpu", space->machine, 0, "sound cpu ctrl: %04x\n", data);
			break;
		default:
            verboselog("maincpu", space->machine, 0, "supracan_sound_w: Unknown register: %08x = %04x & %04x\n", 0xe90000 + (offset << 1), data, mem_mask);
			break;
	}
}


static READ16_HANDLER( supracan_video_r )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	UINT16 data = state->video_regs[offset];

	switch(offset)
	{
		case 0x00/2: // Video IRQ flags
            if(!space->debugger_access())
            {
                //verboselog("maincpu", space->machine, 0, "read video IRQ flags (%04x)\n", data);
                cpu_set_input_line(space->machine->device("maincpu"), 7, CLEAR_LINE);
            }
			break;
        case 0x02/2: // Current scanline
            break;
        case 0x08/2: // Unknown (not video flags!)
            data = 0;
            break;
        case 0x100/2:
            if(!space->debugger_access()) verboselog("maincpu", space->machine, 0, "read tilemap_flags[0] (%04x)\n", data);
            break;
        case 0x106/2:
            if(!space->debugger_access()) verboselog("maincpu", space->machine, 0, "read tilemap_scrolly[0] (%04x)\n", data);
            break;
        case 0x120/2:
            if(!space->debugger_access()) verboselog("maincpu", space->machine, 0, "read tilemap_flags[1] (%04x)\n", data);
            break;
		default:
            if(!space->debugger_access()) verboselog("maincpu", space->machine, 0, "supracan_video_r: Unknown register: %08x (%04x & %04x)\n", 0xf00000 + (offset << 1), data, mem_mask);
			break;
	}

	return data;
}

static TIMER_CALLBACK( supracan_hbl_callback )
{
    supracan_state *state = machine->driver_data<supracan_state>();

    state->hbl_timer->adjust(attotime::never);
}

static TIMER_CALLBACK( supracan_line_on_callback )
{
    supracan_state *state = machine->driver_data<supracan_state>();

    cpu_set_input_line(machine->device("maincpu"), 5, HOLD_LINE);

    state->line_on_timer->adjust(attotime::never);
}

static TIMER_CALLBACK( supracan_line_off_callback )
{
    supracan_state *state = machine->driver_data<supracan_state>();

    cpu_set_input_line(machine->device("maincpu"), 5, CLEAR_LINE);

    state->line_on_timer->adjust(attotime::never);
}

static TIMER_CALLBACK( supracan_video_callback )
{
	supracan_state *state = machine->driver_data<supracan_state>();
	int vpos = machine->primary_screen->vpos();

	state->video_regs[0] &= ~0x0002;

	switch( vpos )
	{
	case 0:
		state->video_regs[0] &= 0x7fff;

		// we really need better management of this
		mark_active_tilemap_all_dirty(machine, 0);
		mark_active_tilemap_all_dirty(machine, 1);
		mark_active_tilemap_all_dirty(machine, 2);
		mark_active_tilemap_all_dirty(machine, 3);


		break;

	case 224://FIXME: Son of Evil is pretty picky about this one, a timing of 240 makes it to crash
		state->video_regs[0] |= 0x8000;
		break;

	case 240:
        if(state->irq_mask & 1)
        {
            verboselog("maincpu", machine, 0, "Triggering VBL IRQ\n\n");
            cpu_set_input_line(machine->device("maincpu"), 7, HOLD_LINE);
        }
		break;
    }

    state->video_regs[1] = machine->primary_screen->vpos()-16; // for son of evil, wants vblank active around 224 instead...

    state->hbl_timer->adjust( machine->primary_screen->time_until_pos( vpos, 320 ) );
    state->video_timer->adjust( machine->primary_screen->time_until_pos( ( vpos + 1 ) % 256, 0 ) );
}

static WRITE16_HANDLER( supracan_video_w )
{
    supracan_state *state = (supracan_state *)space->machine->driver_data<supracan_state>();
	acan_sprdma_regs_t *acan_sprdma_regs = &state->acan_sprdma_regs;
	int i;

	// if any of this changes we need a partial update (see sango fighters intro)
	space->machine->primary_screen->update_partial(space->machine->primary_screen->vpos());

	switch(offset)
	{
        case 0x10/2: // Byte count
            verboselog("maincpu", space->machine, 0, "sprite dma word count: %04x\n", data);
            acan_sprdma_regs->count = data;
            break;
		case 0x12/2: // Destination address MSW
			acan_sprdma_regs->dst &= 0x0000ffff;
			acan_sprdma_regs->dst |= data << 16;
            verboselog("maincpu", space->machine, 0, "sprite dma dest msw: %04x\n", data);
			break;
		case 0x14/2: // Destination address LSW
			acan_sprdma_regs->dst &= 0xffff0000;
			acan_sprdma_regs->dst |= data;
            verboselog("maincpu", space->machine, 0, "sprite dma dest lsw: %04x\n", data);
			break;
        case 0x16/2: // Source word increment
            verboselog("maincpu", space->machine, 0, "sprite dma dest word inc: %04x\n", data);
            acan_sprdma_regs->dst_inc = data;
            break;
        case 0x18/2: // Source address MSW
            acan_sprdma_regs->src &= 0x0000ffff;
            acan_sprdma_regs->src |= data << 16;
            verboselog("maincpu", space->machine, 0, "sprite dma src msw: %04x\n", data);
            break;
        case 0x1a/2: // Source address LSW
            verboselog("maincpu", space->machine, 0, "sprite dma src lsw: %04x\n", data);
            acan_sprdma_regs->src &= 0xffff0000;
            acan_sprdma_regs->src |= data;
            break;
        case 0x1c/2: // Source word increment
            verboselog("maincpu", space->machine, 0, "sprite dma src word inc: %04x\n", data);
            acan_sprdma_regs->src_inc = data;
            break;
		case 0x1e/2:
			logerror( "supracan_video_w: Kicking off a DMA from %08x to %08x, %d bytes (%04x)\n", acan_sprdma_regs->src, acan_sprdma_regs->dst, acan_sprdma_regs->count, data);

			/* TODO: what's 0x2000 and 0x4000 for? */
			if(data & 0x8000)
			{
				if(data & 0x2000 || data & 0x4000)
                {
					acan_sprdma_regs->dst |= 0xf40000;
                }

                if(data & 0x2000)
                {
                    //acan_sprdma_regs->count <<= 1;
                }

				for(i = 0; i <= acan_sprdma_regs->count; i++)
				{
					if(data & 0x0100) //dma 0x00 fill (or fixed value?)
					{
						space->write_word(acan_sprdma_regs->dst, 0);
                        acan_sprdma_regs->dst+=2 * acan_sprdma_regs->dst_inc;
						//memset(supracan_vram,0x00,0x020000);
					}
					else
					{
						space->write_word(acan_sprdma_regs->dst, space->read_word(acan_sprdma_regs->src));
                        acan_sprdma_regs->dst+=2 * acan_sprdma_regs->dst_inc;
                        acan_sprdma_regs->src+=2 * acan_sprdma_regs->src_inc;
					}
				}
			}
			else
			{
                verboselog("maincpu", space->machine, 0, "supracan_dma_w: Attempting to kick off a DMA without bit 15 set! (%04x)\n", data);
			}
			break;
		case 0x08/2:
			{
                verboselog("maincpu", space->machine, 3, "video_flags = %04x\n", data);
                state->video_flags = data;

                rectangle visarea = space->machine->primary_screen->visible_area();

				visarea.min_x = 0;
				visarea.min_y = 8;
				visarea.max_y = 232 - 1;
				visarea.max_x = ((state->video_flags & 0x100) ? 320 : 256) - 1;
				space->machine->primary_screen->configure(348, 256, visarea, space->machine->primary_screen->frame_period().attoseconds);
			}
			break;
        case 0x0a/2:
            {
				// raster interrupt
                verboselog("maincpu", space->machine, 0, "IRQ Trigger? = %04x\n", data);
                if(data & 0x8000)
                {
                    state->line_on_timer->adjust(space->machine->primary_screen->time_until_pos((data & 0x00ff), 0));
                }
                else
                {
                    state->line_on_timer->adjust(attotime::never);
                }
            }
            break;

        case 0x0c/2:
            {
                verboselog("maincpu", space->machine, 0, "IRQ De-Trigger? = %04x\n", data);
                if(data & 0x8000)
                {
                    state->line_off_timer->adjust(space->machine->primary_screen->time_until_pos(data & 0x00ff, 0));
                }
                else
                {
                    state->line_off_timer->adjust(attotime::never);
                }
            }
            break;

        /* Sprites */
		case 0x20/2: state->sprite_base_addr = data << 2; verboselog("maincpu", space->machine, 0, "sprite_base_addr = %04x\n", data); break;
        case 0x22/2: state->sprite_count = data+1; verboselog("maincpu", space->machine, 0, "sprite_count = %d\n", data+1); break;
        case 0x26/2: state->sprite_flags = data; verboselog("maincpu", space->machine, 0, "sprite_flags = %04x\n", data); break;

        /* Tilemap 0 */
        case 0x100/2: state->tilemap_flags[0] = data; verboselog("maincpu", space->machine, 3, "tilemap_flags[0] = %04x\n", data); break;
        case 0x104/2: state->tilemap_scrollx[0] = data; verboselog("maincpu", space->machine, 3, "tilemap_scrollx[0] = %04x\n", data); break;
        case 0x106/2: state->tilemap_scrolly[0] = data; verboselog("maincpu", space->machine, 3, "tilemap_scrolly[0] = %04x\n", data); break;
        case 0x108/2: state->tilemap_base_addr[0] = (data) << 1; verboselog("maincpu", space->machine, 3, "tilemap_base_addr[0] = %05x\n", data << 2); break;
        case 0x10a/2: state->tilemap_mode[0] = data; verboselog("maincpu", space->machine, 3, "tilemap_mode[0] = %04x\n", data); break;

        /* Tilemap 1 */
        case 0x120/2: state->tilemap_flags[1] = data; verboselog("maincpu", space->machine, 3, "tilemap_flags[1] = %04x\n", data); break;
        case 0x124/2: state->tilemap_scrollx[1] = data; verboselog("maincpu", space->machine, 3, "tilemap_scrollx[1] = %04x\n", data); break;
        case 0x126/2: state->tilemap_scrolly[1] = data; verboselog("maincpu", space->machine, 3, "tilemap_scrolly[1] = %04x\n", data); break;
        case 0x128/2: state->tilemap_base_addr[1] = (data) << 1; verboselog("maincpu", space->machine, 3, "tilemap_base_addr[1] = %05x\n", data << 2); break;
        case 0x12a/2: state->tilemap_mode[1] = data; verboselog("maincpu", space->machine, 3, "tilemap_mode[1] = %04x\n", data); break;

        /* Tilemap 2? */
        case 0x140/2: state->tilemap_flags[2] = data; verboselog("maincpu", space->machine, 0, "tilemap_flags[2] = %04x\n", data); break;
        case 0x144/2: state->tilemap_scrollx[2] = data; verboselog("maincpu", space->machine, 0, "tilemap_scrollx[2] = %04x\n", data); break;
        case 0x146/2: state->tilemap_scrolly[2] = data; verboselog("maincpu", space->machine, 0, "tilemap_scrolly[2] = %04x\n", data); break;
        case 0x148/2: state->tilemap_base_addr[2] = (data) << 1; verboselog("maincpu", space->machine, 0, "tilemap_base_addr[2] = %05x\n", data << 2); break;
        case 0x14a/2: state->tilemap_mode[2] = data; verboselog("maincpu", space->machine, 0, "tilemap_mode[2] = %04x\n", data); break;

		/* ROZ */
        case 0x180/2: state->roz_mode = data; verboselog("maincpu", space->machine, 3, "roz_mode = %04x\n", data); break;
        case 0x184/2: state->roz_scrollx = (data << 16) | (state->roz_scrollx & 0xffff); state->roz_changed |= 1; verboselog("maincpu", space->machine, 3, "roz_scrollx = %08x\n", state->roz_scrollx); break;
        case 0x186/2: state->roz_scrollx = (data) | (state->roz_scrollx & 0xffff0000); state->roz_changed |= 1; verboselog("maincpu", space->machine, 3, "roz_scrollx = %08x\n", state->roz_scrollx); break;
        case 0x188/2: state->roz_scrolly = (data << 16) | (state->roz_scrolly & 0xffff); state->roz_changed |= 2; verboselog("maincpu", space->machine, 3, "roz_scrolly = %08x\n", state->roz_scrolly); break;
        case 0x18a/2: state->roz_scrolly = (data) | (state->roz_scrolly & 0xffff0000); state->roz_changed |= 2; verboselog("maincpu", space->machine, 3, "roz_scrolly = %08x\n", state->roz_scrolly); break;
        case 0x18c/2: state->roz_coeffa = data; verboselog("maincpu", space->machine, 3, "roz_coeffa = %04x\n", data); break;
        case 0x18e/2: state->roz_coeffb = data; verboselog("maincpu", space->machine, 3, "roz_coeffb = %04x\n", data); break;
        case 0x190/2: state->roz_coeffc = data; verboselog("maincpu", space->machine, 3, "roz_coeffc = %04x\n", data); break;
        case 0x192/2: state->roz_coeffd = data; verboselog("maincpu", space->machine, 3, "roz_coeffd = %04x\n", data); break;
        case 0x194/2: state->roz_base_addr = (data) << 1; verboselog("maincpu", space->machine, 3, "roz_base_addr = %05x\n", data << 2); break;
        case 0x196/2: state->roz_tile_bank = data; verboselog("maincpu", space->machine, 3, "roz_tile_bank = %04x\n", data); break; //tile bank
        case 0x198/2: state->roz_unk_base0 = data << 2; verboselog("maincpu", space->machine, 3, "roz_unk_base0 = %05x\n", data << 2); break;
        case 0x19a/2: state->roz_unk_base1 = data << 2; verboselog("maincpu", space->machine, 3, "roz_unk_base1 = %05x\n", data << 2); break;
        case 0x19e/2: state->roz_unk_base2 = data << 2; verboselog("maincpu", space->machine, 3, "roz_unk_base2 = %05x\n", data << 2); break;

        case 0x1d0/2: state->unk_1d0 = data; verboselog("maincpu", space->machine, 3, "unk_1d0 = %04x\n", data); break;




		case 0x1f0/2: //FIXME: this register is mostly not understood
			state->irq_mask = data;//(data & 8) ? 0 : 1;
#if 0
            if(!state->irq_mask && !state->hbl_mask)
            {
                cpu_set_input_line(devtag_get_device(space->machine, "maincpu"), 7, CLEAR_LINE);
            }
#endif
            verboselog("maincpu", space->machine, 3, "irq_mask = %04x\n", data);
			break;
		default:
			verboselog("maincpu", space->machine, 0, "supracan_video_w: Unknown register: %08x = %04x & %04x\n", 0xf00000 + (offset << 1), data, mem_mask);
			break;
	}
	state->video_regs[offset] = data;
}


static DEVICE_IMAGE_LOAD( supracan_cart )
{
    UINT8 *cart = image.device().machine->region("cart")->base();
	UINT32 size;

    if (image.software_entry() == NULL)
	{
        size = image.length();

		if (size > 0x400000)
		{
			image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unsupported cartridge size");
            return IMAGE_INIT_FAIL;
		}

		if (image.fread(cart, size) != size)
		{
			image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unable to fully read from file");
            return IMAGE_INIT_FAIL;
		}
	}
	else
	{
		size = image.get_software_region_length("rom");
		memcpy(cart, image.get_software_region("rom"), size);
	}

    return IMAGE_INIT_PASS;
}


static MACHINE_START( supracan )
{
    supracan_state *state = machine->driver_data<supracan_state>();

	state->video_timer = machine->scheduler().timer_alloc(FUNC(supracan_video_callback));
    state->hbl_timer = machine->scheduler().timer_alloc(FUNC(supracan_hbl_callback));
    state->line_on_timer = machine->scheduler().timer_alloc(FUNC(supracan_line_on_callback));
    state->line_off_timer = machine->scheduler().timer_alloc(FUNC(supracan_line_off_callback));
}


static MACHINE_RESET( supracan )
{
    supracan_state *state = machine->driver_data<supracan_state>();

	cputag_set_input_line(machine, "soundcpu", INPUT_LINE_HALT, ASSERT_LINE);

	state->video_timer->adjust( machine->primary_screen->time_until_pos( 0, 0 ) );
	state->irq_mask = 0;
}

/* gfxdecode is retained for reference purposes but not otherwise used by the driver */
static const gfx_layout supracan_gfx8bpp =
{
	8,8,
	RGN_FRAC(1,1),
	8,
	{ 0,1,2,3,4,5,6,7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	{ STEP8(0,8*8) },
	8*8*8
};



static const gfx_layout supracan_gfx4bpp =
{
	8,8,
	RGN_FRAC(1,1),
	4,
	{ 0,1,2,3 },
	{ 0*4, 1*4, 2*4, 3*4, 4*4, 5*4, 6*4, 7*4 },
	{ 0*32, 1*32, 2*32, 3*32, 4*32, 5*32, 6*32, 7*32 },
	8*32
};

static const gfx_layout supracan_gfx2bpp =
{
	8,8,
	RGN_FRAC(1,1),
	2,
	{ 0,1 },
	{ 0*2, 1*2, 2*2, 3*2, 4*2, 5*2, 6*2, 7*2 },
	{ 0*16, 1*16, 2*16, 3*16, 4*16, 5*16, 6*16, 7*16 },
	8*16
};


static const UINT32 xtexlayout_xoffset[64] = { 0,1,2,3,4,5,6,7,          8,9,10,11,12,13,14,15,
                                               16,17,18,19,20,21,22,23,  24,25,26,27,28,29,30,31,
											   32,33,34,35,36,37,38,39,  40,41,42,43,44,45,46,47,
											   48,49,50,51,52,53,54,55,  56,57,58,59,60,61,62,63 };
static const UINT32 xtexlayout_yoffset[64] = {  0*64,1*64,2*64,3*64,4*64,5*64,6*64,7*64,
                                                8*64,9*64,10*64,11*64,12*64,13*64,14*64,15*64,
												16*64,17*64,18*64,19*64,20*64,21*64,22*64,23*64,
												24*64,25*64,26*64,27*64,28*64,29*64,30*64,31*64,
												32*64,33*64,34*64,35*64,36*64,37*64,38*64,39*64,
												40*64,41*64,42*64,43*64,44*64,45*64,46*64,47*64,
												48*64,49*64,50*64,51*64,52*64,53*64,54*64,55*64,
												56*64,57*64,58*64,59*64,60*64,61*64,62*64,63*64 };
static const gfx_layout supracan_gfx1bpp =
{
	64,64,
	RGN_FRAC(1,1),
	1,
	{ 0 },
	EXTENDED_XOFFS,
	EXTENDED_YOFFS,
	64*64,
	xtexlayout_xoffset,
	xtexlayout_yoffset
};

static const gfx_layout supracan_gfx1bpp_alt =
{
	8,8,
	RGN_FRAC(1,1),
	1,
	{ 0 },
	{ 0,1,2,3,4,5,6,7 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8
};


static GFXDECODE_START( supracan )
	GFXDECODE_ENTRY( "ram_gfx2",  0, supracan_gfx8bpp,   0, 1 )
	GFXDECODE_ENTRY( "ram_gfx2",  0, supracan_gfx4bpp,   0, 0x10 )
	GFXDECODE_ENTRY( "ram_gfx2",  0, supracan_gfx2bpp,   0, 0x40 )
	GFXDECODE_ENTRY( "ram_gfx2",  0, supracan_gfx1bpp,   0, 0x80 )
	GFXDECODE_ENTRY( "ram_gfx3",  0, supracan_gfx1bpp_alt,   0, 0x80 )
GFXDECODE_END

static INTERRUPT_GEN( supracan_irq )
{
#if 0
    supracan_state *state = (supracan_state *)device->machine->driver_data<supracan_state>();

    if(state->irq_mask)
    {
        cpu_set_input_line(device, 7, HOLD_LINE);
    }
#endif
}

static INTERRUPT_GEN( supracan_sound_irq )
{
    supracan_state *state = (supracan_state *)device->machine->driver_data<supracan_state>();

    state->sound_irq_source_reg |= 0x80;

    if(state->sound_irq_enable_reg & state->sound_irq_source_reg)
    {
        cpu_set_input_line(device->machine->device("soundcpu"), 0, ASSERT_LINE);
    }
    else
    {
        cpu_set_input_line(device->machine->device("soundcpu"), 0, CLEAR_LINE);
    }
}

static MACHINE_CONFIG_START( supracan, supracan_state )

	MCFG_CPU_ADD( "maincpu", M68000, XTAL_10_738635MHz )		/* Correct frequency unknown */
	MCFG_CPU_PROGRAM_MAP( supracan_mem )
	MCFG_CPU_VBLANK_INT("screen", supracan_irq)

	MCFG_CPU_ADD( "soundcpu", M6502, XTAL_3_579545MHz )		/* TODO: Verfiy actual clock */
	MCFG_CPU_PROGRAM_MAP( supracan_sound_mem )
	MCFG_CPU_VBLANK_INT("screen", supracan_sound_irq)

#if !(SOUNDCPU_BOOT_HACK)
	MCFG_QUANTUM_PERFECT_CPU("maincpu")
	MCFG_QUANTUM_PERFECT_CPU("soundcpu")
#endif

	MCFG_MACHINE_START( supracan )
	MCFG_MACHINE_RESET( supracan )

	MCFG_SCREEN_ADD( "screen", RASTER )
	MCFG_SCREEN_FORMAT( BITMAP_FORMAT_INDEXED16 )
	//MCFG_SCREEN_FORMAT( BITMAP_FORMAT_RGB32 )
	MCFG_SCREEN_RAW_PARAMS(XTAL_10_738635MHz/2, 348, 0, 256, 256, 0, 240 )	/* No idea if this is correct */
	MCFG_SCREEN_UPDATE( supracan )

	MCFG_PALETTE_LENGTH( 32768 )
	MCFG_PALETTE_INIT( supracan )

	MCFG_GFXDECODE(supracan)


	MCFG_CARTSLOT_ADD("cart")
	MCFG_CARTSLOT_EXTENSION_LIST("bin")
	MCFG_CARTSLOT_MANDATORY
	MCFG_CARTSLOT_INTERFACE("supracan_cart")
	MCFG_CARTSLOT_LOAD(supracan_cart)

	MCFG_SOFTWARE_LIST_ADD("cart_list","supracan")

	MCFG_VIDEO_START( supracan )
MACHINE_CONFIG_END


ROM_START( supracan )
	ROM_REGION( 0x400000, "cart", ROMREGION_ERASEFF )

	ROM_REGION( 0x20000, "ram_gfx", ROMREGION_ERASEFF )
	ROM_REGION( 0x20000, "ram_gfx2", ROMREGION_ERASEFF )
	ROM_REGION( 0x20000, "ram_gfx3", ROMREGION_ERASEFF )

ROM_END


/*    YEAR  NAME        PARENT  COMPAT  MACHINE     INPUT     INIT    COMPANY                  FULLNAME        FLAGS */
CONS( 1995, supracan,   0,      0,      supracan,   supracan, 0,      "Funtech Entertainment", "Super A'Can",  GAME_NO_SOUND | GAME_IMPERFECT_GRAPHICS | GAME_NOT_WORKING )
