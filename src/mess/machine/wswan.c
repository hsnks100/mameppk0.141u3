/***************************************************************************

  wswan.c

  Machine file to handle emulation of the Bandai WonderSwan.

  Anthony Kruize
  Wilbert Pol

TODO:
  SRAM sizes should be in kbit instead of kbytes(?). This raises a few
  interesting issues:
  - mirror of smaller <64KBYTE/512kbit sram sizes
  - banking when using 1M or 2M sram sizes

***************************************************************************/

#include "emu.h"
#include "includes/wswan.h"
#include "imagedev/cartslot.h"
#include "image.h"

#define INTERNAL_EEPROM_SIZE	1024

enum enum_system { TYPE_WSWAN=0, TYPE_WSC };
enum enum_sram { SRAM_NONE=0, SRAM_64K, SRAM_256K, SRAM_512K, SRAM_1M, SRAM_2M, EEPROM_1K, EEPROM_16K, EEPROM_8K, SRAM_UNKNOWN };
static const char *const wswan_sram_str[] = { "none", "64Kbit SRAM", "256Kbit SRAM", "512Kbit SRAM", "1Mbit SRAM", "2Mbit SRAM", "1Kbit EEPROM", "16Kbit EEPROM", "8Kbit EEPROM", "Unknown" };
static const int wswan_sram_size[] = { 0, 64*1024/8, 256*1024/8, 512*1024/8, 1024*1024/8, 2*1024*1024/8,  1024/8, 16*1024/8, 8*1024/8, 0 };

static TIMER_CALLBACK(wswan_scanline_interrupt);


static const UINT8 ws_portram_init[256] =
{
	0x00, 0x00, 0x00/*?*/, 0xbb, 0x00, 0x00, 0x00, 0x26, 0xfe, 0xde, 0xf9, 0xfb, 0xdb, 0xd7, 0x7f, 0xf5,
	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x9e, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x99, 0xfd, 0xb7, 0xdf,
	0x30, 0x57, 0x75, 0x76, 0x15, 0x73, 0x70/*77?*/, 0x77, 0x20, 0x75, 0x50, 0x36, 0x70, 0x67, 0x50, 0x77,
	0x57, 0x54, 0x75, 0x77, 0x75, 0x17, 0x37, 0x73, 0x50, 0x57, 0x60, 0x77, 0x70, 0x77, 0x10, 0x73,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
	0x87, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x4f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0xdb, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x42, 0x00, 0x83, 0x00,
	0x2f, 0x3f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1,
	0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1,
	0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1,
	0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1, 0xd1
};

/*
    Some fake bios code to initialize some registers and set up some things on the wonderswan.
    The code from f:ffe0 which gets copied to 0:0400 is taken from a wonderswan crystal's initial
    memory settings. Lacking real bios dumps we will use this....

    The setting of SP to 2000h is what's needed to get Wonderswan Colloseum to boot.

    f000:ffc0
    FC             cld
        BC 00 20       mov sp,2000h
    68 00 00       push 0000h
    07             pop es
    68 00 F0       push F000h
    1F             pop ds
    BF 00 04       mov di,0400h
    BE E0 FF       mov si,FFE0h
    B9 10 00       mov cx,0010h
    F3 A4          rep movsb
    B0 2F          mov al,2Fh
    E6 C0          out al,C0h
    EA 00 04 00 00 jmp 0000:0400

    f000:ffe0
    E4 A0          in al, A0h
    0C 01          or al,01h
    E6 A0          out al,A0h
    EA 00 00 FF FF jmp FFFFh:0000h

*/
static const UINT8 ws_fake_bios_code[] = {
	0xfc, 0xbc, 0x00, 0x20, 0x68, 0x00, 0x00, 0x07, 0x68, 0x00, 0xf0, 0x1f, 0xbf, 0x00, 0x04, 0xbe,
	0xe0, 0xff, 0xb9, 0x10, 0x00, 0xf3, 0xa4, 0xb0, 0x2f, 0xe6, 0xc0, 0xea, 0x00, 0x04, 0x00, 0x00,
	0xe4, 0xa0, 0x0c, 0x01, 0xe6, 0xa0, 0xea, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
	0xea, 0xc0, 0xff, 0x00, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static void wswan_handle_irqs( running_machine *machine )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_HBLTMR ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_HBLTMR );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_VBL ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_VBL );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_VBLTMR ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_VBLTMR );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_LCMP ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_LCMP );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_SRX ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_SRX );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_RTC ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_RTC );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_KEY ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_KEY );
	} else if ( state->ws_portram[0xb2] & state->ws_portram[0xb6] & WSWAN_IFLAG_STX ) {
		cputag_set_input_line_and_vector( machine, "maincpu", 0, HOLD_LINE, state->ws_portram[0xb0] + WSWAN_INT_STX );
	} else {
		cputag_set_input_line(machine, "maincpu", 0, CLEAR_LINE );
	}
}

static void wswan_set_irq_line( running_machine *machine, int irq)
{
	wswan_state *state = machine->driver_data<wswan_state>();
	if ( state->ws_portram[0xb2] & irq )
	{
		state->ws_portram[0xb6] |= irq;
		wswan_handle_irqs( machine );
	}
}

static void wswan_clear_irq_line( running_machine *machine, int irq)
{
	wswan_state *state = machine->driver_data<wswan_state>();
	state->ws_portram[0xb6] &= ~irq;
	wswan_handle_irqs( machine );
}

static TIMER_CALLBACK(wswan_rtc_callback)
{
	wswan_state *state = machine->driver_data<wswan_state>();
	/* A second passed */
	state->rtc.second = state->rtc.second + 1;
	if ( ( state->rtc.second & 0x0F ) > 9 )
	{
		state->rtc.second = ( state->rtc.second & 0xF0 ) + 0x10;
	}

	/* Check for minute passed */
	if ( state->rtc.second >= 0x60 )
	{
		state->rtc.second = 0;
		state->rtc.minute = state->rtc.minute + 1;
		if ( ( state->rtc.minute & 0x0F ) > 9 )
		{
			state->rtc.minute = ( state->rtc.minute & 0xF0 ) + 0x10;
		}
	}

	/* Check for hour passed */
	if ( state->rtc.minute >= 0x60 )
	{
		state->rtc.minute = 0;
		state->rtc.hour = state->rtc.hour + 1;
		if ( ( state->rtc.hour & 0x0F ) > 9 )
		{
			state->rtc.hour = ( state->rtc.hour & 0xF0 ) + 0x10;
		}
		if ( state->rtc.hour == 0x12 )
		{
			state->rtc.hour |= 0x80;
		}
	}

	/* Check for day passed */
	if ( state->rtc.hour >= 0x24 )
	{
		state->rtc.hour = 0;
		state->rtc.day = state->rtc.day + 1;
	}
}

static void wswan_machine_stop( running_machine &machine )
{
	wswan_state *state = machine.driver_data<wswan_state>();
	device_image_interface *image = dynamic_cast<device_image_interface *>(machine.device("cart"));
	if ( state->eeprom.size )
	{
		image->battery_save(state->eeprom.data, state->eeprom.size );
	}
}

static void wswan_setup_bios( running_machine *machine )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	if ( state->ws_bios_bank == NULL )
	{
		state->ws_bios_bank = auto_alloc_array(machine, UINT8, 0x10000 );
		memcpy( state->ws_bios_bank + 0xffc0, ws_fake_bios_code, 0x40 );
	}
}

MACHINE_START( wswan )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	state->ws_bios_bank = NULL;
	state->system_type = TYPE_WSWAN;
	machine->add_notifier(MACHINE_NOTIFY_EXIT, wswan_machine_stop );
	state->vdp.timer = machine->scheduler().timer_alloc(FUNC(wswan_scanline_interrupt), &state->vdp );
	state->vdp.timer->adjust( attotime::from_ticks( 256, 3072000 ), 0, attotime::from_ticks( 256, 3072000 ) );

	wswan_setup_bios(machine);

	/* Set up RTC timer */
	if ( state->rtc.present )
		machine->scheduler().timer_pulse(attotime::from_seconds(1), FUNC(wswan_rtc_callback));
}

MACHINE_START( wscolor )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	state->ws_bios_bank = NULL;
	state->system_type = TYPE_WSC;
	machine->add_notifier(MACHINE_NOTIFY_EXIT, wswan_machine_stop );
	state->vdp.timer = machine->scheduler().timer_alloc(FUNC(wswan_scanline_interrupt), &state->vdp );
	state->vdp.timer->adjust( attotime::from_ticks( 256, 3072000 ), 0, attotime::from_ticks( 256, 3072000 ) );

	wswan_setup_bios(machine);

	/* Set up RTC timer */
	if ( state->rtc.present )
		machine->scheduler().timer_pulse(attotime::from_seconds(1), FUNC(wswan_rtc_callback));
}

MACHINE_RESET( wswan )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	address_space *space = cputag_get_address_space( machine, "maincpu", ADDRESS_SPACE_PROGRAM );

	/* Intialize ports */
	memcpy( state->ws_portram, ws_portram_init, 256 );

	/* Initialize VDP */
	memset( &state->vdp, 0, sizeof( state->vdp ) );

	state->vdp.vram = (UINT8*)space->get_read_ptr(0);
	state->vdp.palette_vram = (UINT8*)space->get_read_ptr(( state->system_type == TYPE_WSC ) ? 0xFE00 : 0 );
	state->vdp.current_line = 145;  /* Randomly chosen, beginning of VBlank period to give cart some time to boot up */
	state->vdp.new_display_vertical = state->ROMMap[state->ROMBanks-1][0xfffc] & 0x01;
	state->vdp.display_vertical = ~state->vdp.new_display_vertical;
	state->vdp.color_mode = 0;
	state->vdp.colors_16 = 0;
	state->vdp.tile_packed = 0;

	/* Initialize sound DMA */
	memset( &state->sound_dma, 0, sizeof( state->sound_dma ) );

	/* Switch in the banks */
	memory_set_bankptr( machine, "bank2", state->ROMMap[(state->ROMBanks - 1) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank3", state->ROMMap[(state->ROMBanks - 1) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank4", state->ROMMap[(state->ROMBanks - 12) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank5", state->ROMMap[(state->ROMBanks - 11) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank6", state->ROMMap[(state->ROMBanks - 10) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank7", state->ROMMap[(state->ROMBanks - 9) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank8", state->ROMMap[(state->ROMBanks - 8) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank9", state->ROMMap[(state->ROMBanks - 7) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank10", state->ROMMap[(state->ROMBanks - 6) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank11", state->ROMMap[(state->ROMBanks - 5) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank12", state->ROMMap[(state->ROMBanks - 4) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank13", state->ROMMap[(state->ROMBanks - 3) & (state->ROMBanks - 1)] );
	memory_set_bankptr( machine, "bank14", state->ROMMap[(state->ROMBanks - 2) & (state->ROMBanks - 1)] );
	state->bios_disabled = 0;
	memory_set_bankptr( machine, "bank15", state->ws_bios_bank );
//  memory_set_bankptr( machine, 15, state->ROMMap[(state->ROMBanks - 1) & (state->ROMBanks - 1)] );
}

NVRAM_HANDLER( wswan )
{
	wswan_state *state = machine->driver_data<wswan_state>();
	if ( read_or_write )
	{
		/* Save the EEPROM data */
		file->write(state->internal_eeprom, INTERNAL_EEPROM_SIZE );
	}
	else
	{
		/* Load the EEPROM data */
		if ( file )
		{
			file->read(state->internal_eeprom, INTERNAL_EEPROM_SIZE );
		}
		else
		{
			/* Initialize the EEPROM data */
			memset( state->internal_eeprom, 0xFF, sizeof( state->internal_eeprom ) );
		}
	}
}

READ8_HANDLER( wswan_sram_r )
{
	wswan_state *state = space->machine->driver_data<wswan_state>();
	if ( state->eeprom.data == NULL )
	{
		return 0xFF;
	}
	return state->eeprom.page[ offset & ( state->eeprom.size - 1 ) ];
}

WRITE8_HANDLER( wswan_sram_w )
{
	wswan_state *state = space->machine->driver_data<wswan_state>();
	if ( state->eeprom.data == NULL )
	{
		return;
	}
	state->eeprom.page[ offset & ( state->eeprom.size - 1 ) ] = data;
}

READ8_HANDLER( wswan_port_r )
{
	wswan_state *state = space->machine->driver_data<wswan_state>();
	UINT8 value = state->ws_portram[offset];

	if ( offset != 2 )
	logerror( "PC=%X: port read %02X\n", cpu_get_pc( space->cpu ), offset );
	switch( offset )
	{
		case 0x02:		/* Current line */
			value = state->vdp.current_line;
			break;
		case 0x4A:		/* Sound DMA source address (low) */
			value = state->sound_dma.source & 0xFF;
			break;
		case 0x4B:		/* Sound DMA source address (high) */
			value = ( state->sound_dma.source >> 8 ) & 0xFF;
			break;
		case 0x4C:		/* Sound DMA source memory segment */
			value = ( state->sound_dma.source >> 16 ) & 0xFF;
			break;
		case 0x4E:		/* Sound DMA transfer size (low) */
			value = state->sound_dma.size & 0xFF;
			break;
		case 0x4F:		/* Sound DMA transfer size (high) */
			value = ( state->sound_dma.size >> 8 ) & 0xFF;
			break;
		case 0x52:		/* Sound DMA start/stop */
			value = state->sound_dma.enable;
			break;
		case 0xA0:		/* Hardware type */
					/* Bit 0 - Disable/enable Bios */
					/* Bit 1 - Determine mono/color */
					/* Bit 2 - Determine color/crystal */
			value = value & ~ 0x02;
			if ( state->system_type == TYPE_WSC )
			{
				value |= 2;
			}
			break;
		case 0xA8:
			value = state->vdp.timer_hblank_count & 0xFF;
			break;
		case 0xA9:
			value = state->vdp.timer_hblank_count >> 8;
			break;
		case 0xAA:
			value = state->vdp.timer_vblank_count & 0xFF;
			break;
		case 0xAB:
			value = state->vdp.timer_vblank_count >> 8;
			break;
		case 0xCB:		/* RTC data */
			if ( state->ws_portram[0xca] == 0x95 && ( state->rtc.index < 7 ) )
			{
				switch( state->rtc.index )
				{
				case 0: value = state->rtc.year; break;
				case 1: value = state->rtc.month; break;
				case 2: value = state->rtc.day; break;
				case 3: value = state->rtc.day_of_week; break;
				case 4: value = state->rtc.hour; break;
				case 5: value = state->rtc.minute; break;
				case 6: value = state->rtc.second; break;
				}
				state->rtc.index++;
			}
	}

	return value;
}

WRITE8_HANDLER( wswan_port_w )
{
	wswan_state *state = space->machine->driver_data<wswan_state>();
	logerror( "PC=%X: port write %02X <- %02X\n", cpu_get_pc( space->cpu ), offset, data );
	switch( offset )
	{
		case 0x00:	/* Display control
                   Bit 0   - Background layer enable
                   Bit 1   - Foreground layer enable
                   Bit 2   - Sprites enable
                   Bit 3   - Sprite window enable
                   Bit 4-5 - Foreground window configuration
                             00 - Foreground layer is displayed inside and outside foreground window area
                             01 - Unknown
                             10 - Foreground layer is displayed only inside foreground window area
                             11 - Foreground layer is displayed outside foreground window area
                   Bit 6-7 - Unknown
                */
			state->vdp.layer_bg_enable = data & 0x1;
			state->vdp.layer_fg_enable = (data & 0x2) >> 1;
			state->vdp.sprites_enable = (data & 0x4) >> 2;
			state->vdp.window_sprites_enable = (data & 0x8) >> 3;
			state->vdp.window_fg_mode = (data & 0x30) >> 4;
			break;
		case 0x01:	/* Background colour
                   In 16 colour mode:
                   Bit 0-3 - Palette index
                   Bit 4-7 - Palette number
                   Otherwise:
                   Bit 0-2 - Main palette index
                   Bit 3-7 - Unknown
                */
			break;
		case 0x02:	/* Current scanline
                   Bit 0-7 - Current scanline (Most likely read-only)
                */
			logerror( "Write to current scanline! Current value: %d  Data to write: %d\n", state->vdp.current_line, data );
			/* Returning so we don't overwrite the value here, not that it
             * really matters */
			return;
		case 0x03:	/* Line compare
                   Bit 0-7 - Line compare
                */
			state->vdp.line_compare = data;
			break;
		case 0x04:	/* Sprite table base address
                   Bit 0-5 - Determine sprite table base address 0 0xxxxxx0 00000000
                   Bit 6-7 - Unknown
                */
			state->vdp.sprite_table_address = ( data & 0x3F ) << 9;
			break;
		case 0x05:	/* Number of sprite to start drawing with
                   Bit 0-7 - First sprite number
                */
			state->vdp.sprite_first = data;
			break;
		case 0x06:	/* Number of sprites to draw
                   Bit 0-7 - Number of sprites to draw
                */
			state->vdp.sprite_count = data;
			break;
		case 0x07:	/* Background/Foreground table base addresses
                   Bit 0-2 - Determine background table base address 00xxx000 00000000
                   Bit 3   - Unknown
                   Bit 4-6 - Determine foreground table base address 00xxx000 00000000
                   Bit 7   - Unknown
                */
			state->vdp.layer_bg_address = (data & 0x7) << 11;
			state->vdp.layer_fg_address = (data & 0x70) << 7;
			break;
		case 0x08:	/* Left coordinate of foreground window
                   Bit 0-7 - Left coordinate of foreground window area
                */
			state->vdp.window_fg_left = data;
			break;
		case 0x09:	/* Top coordinate of foreground window
                   Bit 0-7 - Top coordinatte of foreground window area
                */
			state->vdp.window_fg_top = data;
			break;
		case 0x0A:	/* Right coordinate of foreground window
                   Bit 0-7 - Right coordinate of foreground window area
                */
			state->vdp.window_fg_right = data;
			break;
		case 0x0B:	/* Bottom coordinate of foreground window
                   Bit 0-7 - Bottom coordinate of foreground window area
                */
			state->vdp.window_fg_bottom = data;
			break;
		case 0x0C:	/* Left coordinate of sprite window
                   Bit 0-7 - Left coordinate of sprite window area
                */
			state->vdp.window_sprites_left = data;
			break;
		case 0x0D:	/* Top coordinate of sprite window
                   Bit 0-7 - Top coordinate of sprite window area
                */
			state->vdp.window_sprites_top = data;
			break;
		case 0x0E:	/* Right coordinate of sprite window
                   Bit 0-7 - Right coordinate of sprite window area
                */
			state->vdp.window_sprites_right = data;
			break;
		case 0x0F:	/* Bottom coordinate of sprite window
                   Bit 0-7 - Bottom coordiante of sprite window area
                */
			state->vdp.window_sprites_bottom = data;
			break;
		case 0x10:	/* Background layer X scroll
                   Bit 0-7 - Background layer X scroll
                */
			state->vdp.layer_bg_scroll_x = data;
			break;
		case 0x11:	/* Background layer Y scroll
                   Bit 0-7 - Background layer Y scroll
                */
			state->vdp.layer_bg_scroll_y = data;
			break;
		case 0x12:	/* Foreground layer X scroll
                   Bit 0-7 - Foreground layer X scroll
                */
			state->vdp.layer_fg_scroll_x = data;
			break;
		case 0x13:	/* Foreground layer Y scroll
                   Bit 0-7 - Foreground layer Y scroll
                */
			state->vdp.layer_fg_scroll_y = data;
			break;
		case 0x14:	/* LCD control
                   Bit 0   - LCD enable
                   Bit 1-7 - Unknown
                */
			state->vdp.lcd_enable = data & 0x1;
			break;
		case 0x15:	/* LCD icons
                   Bit 0   - LCD sleep icon enable
                   Bit 1   - Vertical position icon enable
                   Bit 2   - Horizontal position icon enable
                   Bit 3   - Dot 1 icon enable
                   Bit 4   - Dot 2 icon enable
                   Bit 5   - Dot 3 icon enable
                   Bit 6-7 - Unknown
                */
			state->vdp.icons = data;	/* ummmmm */
			break;
		case 0x1c:	/* Palette colors 0 and 1
                   Bit 0-3 - Gray tone setting for main palette index 0
                   Bit 4-7 - Gray tone setting for main palette index 1
                */
			if ( state->system_type == TYPE_WSC )
			{
				int i = 15 - ( data & 0x0F );
				int j = 15 - ( ( data & 0xF0 ) >> 4 );
				state->vdp.main_palette[0] = ( i << 8 ) | ( i << 4 ) | i;
				state->vdp.main_palette[1] = ( j << 8 ) | ( j << 4 ) | j;
			}
			else
			{
				state->vdp.main_palette[0] = data & 0x0F;
				state->vdp.main_palette[1] = ( data & 0xF0 ) >> 4;
			}
			break;
		case 0x1d:	/* Palette colors 2 and 3
                   Bit 0-3 - Gray tone setting for main palette index 2
                   Bit 4-7 - Gray tone setting for main palette index 3
                */
			if ( state->system_type == TYPE_WSC )
			{
				int i = 15 - ( data & 0x0F );
				int j = 15 - ( ( data & 0xF0 ) >> 4 );
				state->vdp.main_palette[2] = ( i << 8 ) | ( i << 4 ) | i;
				state->vdp.main_palette[3] = ( j << 8 ) | ( j << 4 ) | j;
			}
			else
			{
				state->vdp.main_palette[2] = data & 0x0F;
				state->vdp.main_palette[3] = ( data & 0xF0 ) >> 4;
			}
			break;
		case 0x1e:	/* Palette colors 4 and 5
                   Bit 0-3 - Gray tone setting for main palette index 4
                   Bit 4-7 - Gray tone setting for main paeltte index 5
                */
			if ( state->system_type == TYPE_WSC )
			{
				int i = 15 - ( data & 0x0F );
				int j = 15 - ( ( data & 0xF0 ) >> 4 );
				state->vdp.main_palette[4] = ( i << 8 ) | ( i << 4 ) | i;
				state->vdp.main_palette[5] = ( j << 8 ) | ( j << 4 ) | j;
			}
			else
			{
				state->vdp.main_palette[4] = data & 0x0F;
				state->vdp.main_palette[5] = ( data & 0xF0 ) >> 4;
			}
			break;
		case 0x1f:	/* Palette colors 6 and 7
                   Bit 0-3 - Gray tone setting for main palette index 6
                   Bit 4-7 - Gray tone setting for main palette index 7
                */
			if ( state->system_type == TYPE_WSC )
			{
				int i = 15 - ( data & 0x0F );
				int j = 15 - ( ( data & 0xF0 ) >> 4 );
				state->vdp.main_palette[6] = ( i << 8 ) | ( i << 4 ) | i;
				state->vdp.main_palette[7] = ( j << 8 ) | ( j << 4 ) | j;
			}
			else
			{
				state->vdp.main_palette[6] = data & 0x0F;
				state->vdp.main_palette[7] = ( data & 0xF0 ) >> 4;
			}
			break;
		case 0x20:	/* tile/sprite palette settings
                   Bit 0-3 - Palette 0 index 0
                   Bit 4-7 - Palette 0 index 1 */
		case 0x21:	/* Bit 0-3 - Palette 0 index 2
                   Bit 4-7 - Palette 0 index 3 */
		case 0x22:	/* Bit 0-3 - Palette 1 index 0
                   Bit 4-7 - Palette 1 index 1 */
		case 0x23:	/* Bit 0-3 - Palette 1 index 2
                   Bit 4-7 - Palette 1 index 3 */
		case 0x24:	/* Bit 0-3 - Palette 2 index 0
                   Bit 4-7 - Palette 2 index 1 */
		case 0x25:	/* Bit 0-3 - Palette 2 index 2
                   Bit 4-7 - Palette 2 index 3 */
		case 0x26:	/* Bit 0-3 - Palette 3 index 0
                   Bit 4-7 - Palette 3 index 1 */
		case 0x27:	/* Bit 0-3 - Palette 3 index 2
                   Bit 4-7 - Palette 3 index 3 */
		case 0x28:	/* Bit 0-3 - Palette 4 index 0
                   Bit 4-7 - Palette 4 index 1 */
		case 0x29:	/* Bit 0-3 - Palette 4 index 2
                   Bit 4-7 - Palette 4 index 3 */
		case 0x2A:	/* Bit 0-3 - Palette 5 index 0
                   Bit 4-7 - Palette 5 index 1 */
		case 0x2B:	/* Bit 0-3 - Palette 5 index 2
                   Bit 4-7 - Palette 5 index 3 */
		case 0x2C:	/* Bit 0-3 - Palette 6 index 0
                   Bit 4-7 - Palette 6 index 1 */
		case 0x2D:	/* Bit 0-3 - Palette 6 index 2
                   Bit 4-7 - Palette 6 index 3 */
		case 0x2E:	/* Bit 0-3 - Palette 7 index 0
                   Bit 4-7 - Palette 7 index 1 */
		case 0x2F:	/* Bit 0-3 - Palette 7 index 2
                   Bit 4-7 - Palette 7 index 3 */
		case 0x30:	/* Bit 0-3 - Palette 8 / Sprite Palette 0 index 0
                   Bit 4-7 - Palette 8 / Sprite Palette 0 index 1 */
		case 0x31:	/* Bit 0-3 - Palette 8 / Sprite Palette 0 index 2
                   Bit 4-7 - Palette 8 / Sprite Palette 0 index 3 */
		case 0x32:	/* Bit 0-3 - Palette 9 / Sprite Palette 1 index 0
                   Bit 4-7 - Palette 9 / Sprite Palette 1 index 1 */
		case 0x33:	/* Bit 0-3 - Palette 9 / Sprite Palette 1 index 2
                   Bit 4-7 - Palette 9 / Sprite Palette 1 index 3 */
		case 0x34:	/* Bit 0-3 - Palette 10 / Sprite Palette 2 index 0
                   Bit 4-7 - Palette 10 / Sprite Palette 2 index 1 */
		case 0x35:	/* Bit 0-3 - Palette 10 / Sprite Palette 2 index 2
                   Bit 4-7 - Palette 10 / Sprite Palette 2 index 3 */
		case 0x36:	/* Bit 0-3 - Palette 11 / Sprite Palette 3 index 0
                   Bit 4-7 - Palette 11 / Sprite Palette 3 index 1 */
		case 0x37:	/* Bit 0-3 - Palette 11 / Sprite Palette 3 index 2
                   Bit 4-7 - Palette 11 / Sprite Palette 3 index 3 */
		case 0x38:	/* Bit 0-3 - Palette 12 / Sprite Palette 4 index 0
                   Bit 4-7 - Palette 12 / Sprite Palette 4 index 1 */
		case 0x39:	/* Bit 0-3 - Palette 12 / Sprite Palette 4 index 2
                   Bit 4-7 - Palette 12 / Sprite Palette 4 index 3 */
		case 0x3A:	/* Bit 0-3 - Palette 13 / Sprite Palette 5 index 0
                   Bit 4-7 - Palette 13 / Sprite Palette 5 index 1 */
		case 0x3B:	/* Bit 0-3 - Palette 13 / Sprite Palette 5 index 2
                   Bit 4-7 - Palette 13 / Sprite Palette 5 index 3 */
		case 0x3C:	/* Bit 0-3 - Palette 14 / Sprite Palette 6 index 0
                   Bit 4-7 - Palette 14 / Sprite Palette 6 index 1 */
		case 0x3D:	/* Bit 0-3 - Palette 14 / Sprite Palette 6 index 2
                   Bit 4-7 - Palette 14 / Sprite Palette 6 index 3 */
		case 0x3E:	/* Bit 0-3 - Palette 15 / Sprite Palette 7 index 0
                   Bit 4-7 - Palette 15 / Sprite Palette 7 index 1 */
		case 0x3F:	/* Bit 0-3 - Palette 15 / Sprite Palette 7 index 2
                   Bit 4-7 - Palette 15 / Sprite Palette 7 index 3 */
			break;
		case 0x40:	/* DMA source address (low)
                   Bit 0-7 - DMA source address bit 0-7
                */
		case 0x41:	/* DMA source address (high)
                   Bit 0-7 - DMA source address bit 8-15
                */
		case 0x42:	/* DMA source bank
                   Bit 0-7 - DMA source bank number
                */
		case 0x43:	/* DMA destination bank
                   Bit 0-7 - DMA destination bank number
                */
		case 0x44:	/* DMA destination address (low)
                   Bit 0-7 - DMA destination address bit 0-7
                */
		case 0x45:	/* DMA destination address (high)
                   Bit 0-7 - DMA destination address bit 8-15
                */
		case 0x46:	/* Size of copied data (low)
                   Bit 0-7 - DMA size bit 0-7
                */
		case 0x47:	/* Size of copied data (high)
                   Bit 0-7 - DMA size bit 8-15
                */
			break;
		case 0x48:	/* DMA control
                   Bit 0-6 - Unknown
                   Bit 7   - DMA stop/start
                */
			if( data & 0x80 )
			{
				UINT32 src, dst;
				UINT16 length;

				src = state->ws_portram[0x40] + (state->ws_portram[0x41] << 8) + (state->ws_portram[0x42] << 16);
				dst = state->ws_portram[0x44] + (state->ws_portram[0x45] << 8) + (state->ws_portram[0x43] << 16);
				length = state->ws_portram[0x46] + (state->ws_portram[0x47] << 8);
				for( ; length > 0; length-- )
				{
					space->write_byte(dst, space->read_byte(src ) );
					src++;
					dst++;
				}
#ifdef DEBUG
					logerror( "DMA  src:%X dst:%X length:%d\n", src, dst, length );
#endif
				state->ws_portram[0x40] = src & 0xFF;
				state->ws_portram[0x41] = ( src >> 8 ) & 0xFF;
				state->ws_portram[0x44] = dst & 0xFF;
				state->ws_portram[0x45] = ( dst >> 8 ) & 0xFF;
				state->ws_portram[0x46] = length & 0xFF;
				state->ws_portram[0x47] = ( length >> 8 ) & 0xFF;
				data &= 0x7F;
			}
			break;
		case 0x4A:	/* Sound DMA source address (low)
                   Bit 0-7 - Sound DMA source address bit 0-7
                */
			state->sound_dma.source = ( state->sound_dma.source & 0x0FFF00 ) | data;
			break;
		case 0x4B:	/* Sound DMA source address (high)
                   Bit 0-7 - Sound DMA source address bit 8-15
                */
			state->sound_dma.source = ( state->sound_dma.source & 0x0F00FF ) | ( data << 8 );
			break;
		case 0x4C:	/* Sound DMA source memory segment
                   Bit 0-3 - Sound DMA source address segment
                   Bit 4-7 - Unknown
                */
			state->sound_dma.source = ( state->sound_dma.source & 0xFFFF ) | ( ( data & 0x0F ) << 16 );
			break;
		case 0x4D:	/* Unknown */
			break;
		case 0x4E:	/* Sound DMA transfer size (low)
                   Bit 0-7 - Sound DMA transfer size bit 0-7
                */
			state->sound_dma.size = ( state->sound_dma.size & 0xFF00 ) | data;
			break;
		case 0x4F:	/* Sound DMA transfer size (high)
                   Bit 0-7 - Sound DMA transfer size bit 8-15
                */
			state->sound_dma.size = ( state->sound_dma.size & 0xFF ) | ( data << 8 );
			break;
		case 0x50:	/* Unknown */
		case 0x51:	/* Unknown */
			break;
		case 0x52:	/* Sound DMA start/stop
                   Bit 0-6 - Unknown
                   Bit 7   - Sound DMA stop/start
                */
			state->sound_dma.enable = data;
			break;
		case 0x60:	/* Video mode
                   Bit 0-4 - Unknown
                   Bit 5   - Packed mode 0 = not packed mode, 1 = packed mode
                   Bit 6   - 4/16 colour mode select: 0 = 4 colour mode, 1 = 16 colour mode
                   Bit 7   - monochrome/colour mode select: 0 = monochrome mode, 1 = colour mode
                */
			/*
             * 111  - packed, 16 color, use 4000/8000, color
             * 110  - not packed, 16 color, use 4000/8000, color
             * 101  - packed, 4 color, use 2000, color
             * 100  - not packed, 4 color, use 2000, color
             * 011  - packed, 16 color, use 4000/8000, monochrome
             * 010  - not packed, 16 color , use 4000/8000, monochrome
             * 001  - packed, 4 color, use 2000, monochrome
             * 000  - not packed, 4 color, use 2000, monochrome - Regular WS monochrome
             */
			if ( state->system_type == TYPE_WSC )
			{
				state->vdp.color_mode = data & 0x80;
				state->vdp.colors_16 = data & 0x40;
				state->vdp.tile_packed = data & 0x20;
			}
			break;
		case 0x80:	/* Audio 1 freq (lo)
                   Bit 0-7 - Audio channel 1 frequency bit 0-7
                */
		case 0x81:	/* Audio 1 freq (hi)
                   Bit 0-7 - Audio channel 1 frequency bit 8-15
                */
		case 0x82:	/* Audio 2 freq (lo)
                   Bit 0-7 - Audio channel 2 frequency bit 0-7
                */
		case 0x83:	/* Audio 2 freq (hi)
                   Bit 0-7 - Audio channel 2 frequency bit 8-15
                */
		case 0x84:	/* Audio 3 freq (lo)
                   Bit 0-7 - Audio channel 3 frequency bit 0-7
                */
		case 0x85:	/* Audio 3 freq (hi)
                   Bit 0-7 - Audio channel 3 frequency bit 8-15
                */
		case 0x86:	/* Audio 4 freq (lo)
                   Bit 0-7 - Audio channel 4 frequency bit 0-7
                */
		case 0x87:	/* Audio 4 freq (hi)
                   Bit 0-7 - Audio channel 4 frequency bit 8-15
                */
		case 0x88:	/* Audio 1 volume
                   Bit 0-3 - Right volume audio channel 1
                   Bit 4-7 - Left volume audio channel 1
                */
		case 0x89:	/* Audio 2 volume
                   Bit 0-3 - Right volume audio channel 2
                   Bit 4-7 - Left volume audio channel 2
                */
		case 0x8A:	/* Audio 3 volume
                   Bit 0-3 - Right volume audio channel 3
                   Bit 4-7 - Left volume audio channel 3
                */
		case 0x8B:	/* Audio 4 volume
                   Bit 0-3 - Right volume audio channel 4
                   Bit 4-7 - Left volume audio channel 4
                */
		case 0x8C:	/* Sweep step
                   Bit 0-7 - Sweep step
                */
		case 0x8D:	/* Sweep time
                   Bit 0-7 - Sweep time
                */
		case 0x8E:	/* Noise control
                   Bit 0-2 - Noise generator type
                   Bit 3   - Reset
                   Bit 4   - Enable
                   Bit 5-7 - Unknown
                */
		case 0x8F:	/* Sample location
                   Bit 0-7 - Sample address location 0 00xxxxxx xx000000
                */
		case 0x90:	/* Audio control
                   Bit 0   - Audio 1 enable
                   Bit 1   - Audio 2 enable
                   Bit 2   - Audio 3 enable
                   Bit 3   - Audio 4 enable
                   Bit 4   - Unknown
                   Bit 5   - Audio 2 voice mode enable
                   Bit 6   - Audio 3 sweep mode enable
                   Bit 7   - Audio 4 noise mode enable
                */
		case 0x91:	/* Audio output
                   Bit 0   - Mono select
                   Bit 1-2 - Output volume
                   Bit 3   - External stereo
                   Bit 4-6 - Unknown
                   Bit 7   - External speaker (Read-only, set by hardware)
                */
		case 0x92:	/* Noise counter shift register (lo)
                   Bit 0-7 - Noise counter shift register bit 0-7
                */
		case 0x93:	/* Noise counter shift register (hi)
                   Bit 0-6 - Noise counter shift register bit 8-14
                   bit 7   - Unknown
                */
		case 0x94:	/* Master volume
                   Bit 0-3 - Master volume
                   Bit 4-7 - Unknown
                */
			wswan_sound_port_w( space->machine->device("custom"), offset, data );
			break;
		case 0xa0:	/* Hardware type - this is probably read only
                   Bit 0   - Enable cartridge slot and/or disable bios
                   Bit 1   - Hardware type: 0 = WS, 1 = WSC
                   Bit 2-7 - Unknown
                */
			if ( ( data & 0x01 ) && !state->bios_disabled )
			{
				state->bios_disabled = 1;
				memory_set_bankptr( space->machine, "bank15", state->ROMMap[ ( ( ( state->ws_portram[0xc0] & 0x0F ) << 4 ) | 15 ) & ( state->ROMBanks - 1 ) ] );
			}
			break;
		case 0xa2:	/* Timer control
                   Bit 0   - HBlank Timer enable
                   Bit 1   - HBlank Timer mode: 0 = one shot, 1 = auto reset
                   Bit 2   - VBlank Timer(1/75s) enable
                   Bit 3   - VBlank Timer mode: 0 = one shot, 1 = auto reset
                   Bit 4-7 - Unknown
                */
			state->vdp.timer_hblank_enable = data & 0x1;
			state->vdp.timer_hblank_mode = (data & 0x2) >> 1;
			state->vdp.timer_vblank_enable = (data & 0x4) >> 2;
			state->vdp.timer_vblank_mode = (data & 0x8) >> 3;
			break;
		case 0xa4:	/* HBlank timer frequency (low) - reload value
                   Bit 0-7 - HBlank timer reload value bit 0-7
                */
			state->vdp.timer_hblank_reload &= 0xff00;
			state->vdp.timer_hblank_reload += data;
			state->vdp.timer_hblank_count = state->vdp.timer_hblank_reload;
			break;
		case 0xa5:	/* HBlank timer frequency (high) - reload value
                   Bit 8-15 - HBlank timer reload value bit 8-15
                */
			state->vdp.timer_hblank_reload &= 0xff;
			state->vdp.timer_hblank_reload += data << 8;
			state->vdp.timer_hblank_count = state->vdp.timer_hblank_reload;
			break;
		case 0xa6:	/* VBlank timer frequency (low) - reload value
                   Bit 0-7 - VBlank timer reload value bit 0-7
                */
			state->vdp.timer_vblank_reload &= 0xff00;
			state->vdp.timer_vblank_reload += data;
			state->vdp.timer_vblank_count = state->vdp.timer_vblank_reload;
			break;
		case 0xa7:	/* VBlank timer frequency (high) - reload value
                   Bit 0-7 - VBlank timer reload value bit 8-15
                */
			state->vdp.timer_vblank_reload &= 0xff;
			state->vdp.timer_vblank_reload += data << 8;
			state->vdp.timer_vblank_count = state->vdp.timer_vblank_reload;
			break;
		case 0xa8:	/* HBlank counter (low)
                   Bit 0-7 - HBlank counter bit 0-7
                */
		case 0xa9:	/* HBlank counter (high)
                   Bit 0-7 - HBlank counter bit 8-15
                */
		case 0xaa:	/* VBlank counter (low)
                   Bit 0-7 - VBlank counter bit 0-7
                */
		case 0xab:	/* VBlank counter (high)
                   Bit 0-7 - VBlank counter bit 8-15
                */
			break;

		case 0xb0:	/* Interrupt base vector
                   Bit 0-7 - Interrupt base vector
                */
			break;
		case 0xb1:	/* Communication byte
                   Bit 0-7 - Communication byte
                */
			break;
		case 0xb2:	/* Interrupt enable
                   Bit 0   - Serial transmit interrupt enable
                   Bit 1   - Key press interrupt enable
                   Bit 2   - RTC alarm interrupt enable
                   Bit 3   - Serial receive interrupt enable
                   Bit 4   - Drawing line detection interrupt enable
                   Bit 5   - VBlank timer interrupt enable
                   Bit 6   - VBlank interrupt enable
                   Bit 7   - HBlank timer interrupt enable
                */
			break;
		case 0xb3:	/* serial communication control
                   Bit 0   - Receive complete
                   Bit 1   - Error
                   Bit 2   - Send complete
                   Bit 3-4 - Unknown
                   Bit 5   - Send data interrupt generation
                   Bit 6   - Connection speed: 0 = 9600 bps, 1 = 38400 bps
                   bit 7   - Receive data interrupt generation
                */
//          data |= 0x02;
			state->ws_portram[0xb1] = 0xFF;
			if ( data & 0x80 )
			{
//              state->ws_portram[0xb1] = 0x00;
				data |= 0x04;
			}
			if (data & 0x20 )
			{
//              data |= 0x01;
			}
			break;
		case 0xb5:	/* Read controls
                   Bit 0-3 - Current state of input lines (read-only)
                   Bit 4-6 - Select line of inputs to read
                             001 - Read Y cursors
                             010 - Read X cursors
                             100 - Read START,A,B buttons
                   Bit 7   - Unknown
                */
			data = data & 0xF0;
			switch( data )
			{
			case 0x10:	/* Read Y cursors: Y1 - Y2 - Y3 - Y4 */
				data = data | input_port_read(space->machine, "CURSY");
				break;
			case 0x20:	/* Read X cursors: X1 - X2 - X3 - X4 */
				data = data | input_port_read(space->machine, "CURSX");
				break;
			case 0x40:	/* Read buttons: START - A - B */
				data = data | input_port_read(space->machine, "BUTTONS");
				break;
			}
			break;
		case 0xb6:	/* Interrupt acknowledge
                   Bit 0   - Serial transmit interrupt acknowledge
                   Bit 1   - Key press interrupt acknowledge
                   Bit 2   - RTC alarm interrupt acknowledge
                   Bit 3   - Serial receive interrupt acknowledge
                   Bit 4   - Drawing line detection interrupt acknowledge
                   Bit 5   - VBlank timer interrupt acknowledge
                   Bit 6   - VBlank interrupt acknowledge
                   Bit 7   - HBlank timer interrupt acknowledge
                */
			wswan_clear_irq_line( space->machine, data );
			data = state->ws_portram[0xB6];
			break;
		case 0xba:	/* Internal EEPROM data (low)
                   Bit 0-7 - Internal EEPROM data transfer bit 0-7
                */
		case 0xbb:	/* Internal EEPROM data (high)
                   Bit 0-7 - Internal EEPROM data transfer bit 8-15
                */
			break;
		case 0xbc:	/* Internal EEPROM address (low)
                   Bit 0-7 - Internal EEPROM address bit 1-8
                */
		case 0xbd:	/* Internal EEPROM address (high)
                   Bit 0   - Internal EEPROM address bit 9(?)
                   Bit 1-7 - Unknown
                   Only 1KByte internal EEPROM??
                */
			break;
		case 0xbe:	/* Internal EEPROM command
                   Bit 0   - Read complete (read only)
                   Bit 1   - Write complete (read only)
                   Bit 2-3 - Unknown
                   Bit 4   - Read
                   Bit 5   - Write
                   Bit 6   - Protect
                   Bit 7   - Initialize
                */
			if ( data & 0x20 )
			{
				UINT16 addr = ( ( ( state->ws_portram[0xbd] << 8 ) | state->ws_portram[0xbc] ) << 1 ) & 0x1FF;
				state->internal_eeprom[ addr ] = state->ws_portram[0xba];
				state->internal_eeprom[ addr + 1 ] = state->ws_portram[0xbb];
				data |= 0x02;
			}
			else if ( data & 0x10 )
			{
				UINT16 addr = ( ( ( state->ws_portram[0xbd] << 8 ) | state->ws_portram[0xbc] ) << 1 ) & 0x1FF;
				state->ws_portram[0xba] = state->internal_eeprom[ addr ];
				state->ws_portram[0xbb] = state->internal_eeprom[ addr + 1];
				data |= 0x01;
			}
			else
			{
				logerror( "Unsupported internal EEPROM command: %X\n", data );
			}
			break;
		case 0xc0:	/* ROM bank select for banks 4-15
                   Bit 0-3 - ROM bank base register for banks 4-15
                   Bit 4-7 - Unknown
                */
			memory_set_bankptr( space->machine, "bank4", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 4 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank5", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 5 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank6", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 6 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank7", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 7 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank8", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 8 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank9", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 9 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank10", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 10 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank11", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 11 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank12", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 12 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank13", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 13 ) & ( state->ROMBanks - 1 ) ] );
			memory_set_bankptr( space->machine, "bank14", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 14 ) & ( state->ROMBanks - 1 ) ] );
			if ( state->bios_disabled )
			{
				memory_set_bankptr( space->machine, "bank15", state->ROMMap[ ( ( ( data & 0x0F ) << 4 ) | 15 ) & ( state->ROMBanks - 1 ) ] );
			}
			break;
		case 0xc1:	/* SRAM bank select
                   Bit 0-7 - SRAM bank to select
                */
			if ( state->eeprom.mode == SRAM_64K || state->eeprom.mode == SRAM_256K || state->eeprom.mode == SRAM_512K || state->eeprom.mode == SRAM_1M || state->eeprom.mode == SRAM_2M )
			{
				state->eeprom.page = &state->eeprom.data[ ( data * 64 * 1024 ) & ( state->eeprom.size - 1 ) ];
			}
			break;
		case 0xc2:	/* ROM bank select for segment 2 (0x20000 - 0x2ffff)
                   Bit 0-7 - ROM bank for segment 2
                */
			memory_set_bankptr( space->machine, "bank2", state->ROMMap[ data & ( state->ROMBanks - 1 ) ]);
			break;
		case 0xc3:	/* ROM bank select for segment 3 (0x30000-0x3ffff)
                   Bit 0-7 - ROM bank for segment 3
                */
			memory_set_bankptr( space->machine, "bank3", state->ROMMap[ data & ( state->ROMBanks - 1 ) ]);
			break;
		case 0xc6:	/* EEPROM address lower bits port/EEPROM address and command port
                   1KBit EEPROM:
                   Bit 0-5 - EEPROM address bit 1-6
                   Bit 6-7 - Command
                             00 - Extended command address bit 4-5:
                                  00 - Write disable
                                  01 - Write all
                                  10 - Erase all
                                  11 - Write enable
                             01 - Write
                             10 - Read
                             11 - Erase
                   16KBit EEPROM:
                   Bit 0-7 - EEPROM address bit 1-8
                */
			switch( state->eeprom.mode )
			{
			case EEPROM_1K:
				state->eeprom.address = data & 0x3F;
				state->eeprom.command = data >> 4;
				if ( ( state->eeprom.command & 0x0C ) != 0x00 )
				{
					state->eeprom.command = state->eeprom.command & 0x0C;
				}
				break;
			case EEPROM_16K:
				state->eeprom.address = ( state->eeprom.address & 0xFF00 ) | data;
				break;
			default:
				logerror( "Write EEPROM address/register register C6 for unsupported EEPROM type\n" );
				break;
			}
			break;
		case 0xc7:	/* EEPROM higher bits/command bits port
                   1KBit EEPROM:
                   Bit 0   - Start
                   Bit 1-7 - Unknown
                   16KBit EEPROM:
                   Bit 0-1 - EEPROM address bit 9-10
                   Bit 2-3 - Command
                             00 - Extended command address bit 0-1:
                                  00 - Write disable
                                  01 - Write all
                                  10 - Erase all
                                  11 - Write enable
                             01 - Write
                             10 - Read
                         11 - Erase
                   Bit 4   - Start
                   Bit 5-7 - Unknown
                */
			switch( state->eeprom.mode )
			{
			case EEPROM_1K:
				state->eeprom.start = data & 0x01;
				break;
			case EEPROM_16K:
				state->eeprom.address = ( ( data & 0x03 ) << 8 ) | ( state->eeprom.address & 0xFF );
				state->eeprom.command = data & 0x0F;
				if ( ( state->eeprom.command & 0x0C ) != 0x00 )
				{
					state->eeprom.command = state->eeprom.command & 0x0C;
				}
				state->eeprom.start = ( data >> 4 ) & 0x01;
				break;
			default:
				logerror( "Write EEPROM address/command register C7 for unsupported EEPROM type\n" );
				break;
			}
			break;
		case 0xc8:	/* EEPROM command
                   Bit 0   - Read complete (read only)
                   Bit 1   - Write complete (read only)
                   Bit 2-3 - Unknown
                   Bit 4   - Read
                   Bit 5   - Write
                   Bit 6   - Protect
                   Bit 7   - Initialize
                */
			if ( state->eeprom.mode == EEPROM_1K || state->eeprom.mode == EEPROM_16K )
			{
				if ( data & 0x80 )
				{	/* Initialize */
					logerror( "Unsupported EEPROM command 'Initialize'\n" );
				}
				if ( data & 0x40 )
				{	/* Protect */
					switch( state->eeprom.command )
					{
					case 0x00:
						state->eeprom.write_enabled = 0;
						data |= 0x02;
						break;
					case 0x03:
						state->eeprom.write_enabled = 1;
						data |= 0x02;
						break;
					default:
						logerror( "Unsupported 'Protect' command %X\n", state->eeprom.command );
					}
				}
				if ( data & 0x20 )
				{	/* Write */
					if ( state->eeprom.write_enabled )
					{
						switch( state->eeprom.command )
						{
						case 0x04:
							state->eeprom.data[ ( state->eeprom.address << 1 ) + 1 ] = state->ws_portram[0xc4];
							state->eeprom.data[ state->eeprom.address << 1 ] = state->ws_portram[0xc5];
							data |= 0x02;
							break;
						default:
							logerror( "Unsupported 'Write' command %X\n", state->eeprom.command );
						}
					}
				}
				if ( data & 0x10 )
				{	/* Read */
					state->ws_portram[0xc4] = state->eeprom.data[ ( state->eeprom.address << 1 ) + 1 ];
					state->ws_portram[0xc5] = state->eeprom.data[ state->eeprom.address << 1 ];
					data |= 0x01;
				}
			}
			else
			{
				logerror( "EEPROM command for unknown EEPROM type\n" );
			}
			break;
		case 0xca:	/* RTC Command
                   Bit 0-4 - RTC command
                             10000 - Reset
                             10010 - Write timer settings (alarm)
                             10011 - Read timer settings (alarm)
                             10100 - Set time/date
                             10101 - Get time/date
                   Bit 5-6 - Unknown
                   Bit 7   - Command done (read only)
                */
			switch( data )
			{
			case 0x10:	/* Reset */
				state->rtc.index = 8;
				state->rtc.year = 0;
				state->rtc.month = 1;
				state->rtc.day = 1;
				state->rtc.day_of_week = 0;
				state->rtc.hour = 0;
				state->rtc.minute = 0;
				state->rtc.second = 0;
				state->rtc.setting = 0xFF;
				data |= 0x80;
				break;
			case 0x12:	/* Write Timer Settings (Alarm) */
				state->rtc.index = 8;
				state->rtc.setting = state->ws_portram[0xcb];
				data |= 0x80;
				break;
			case 0x13:	/* Read Timer Settings (Alarm) */
				state->rtc.index = 8;
				state->ws_portram[0xcb] = state->rtc.setting;
				data |= 0x80;
				break;
			case 0x14:	/* Set Time/Date */
				state->rtc.year = state->ws_portram[0xcb];
				state->rtc.index = 1;
				data |= 0x80;
				break;
			case 0x15:	/* Get Time/Date */
				state->rtc.index = 0;
				data |= 0x80;
				state->ws_portram[0xcb] = state->rtc.year;
				break;
			default:
				logerror( "%X: Unknown RTC command (%X) requested\n", cpu_get_pc( space->cpu ), data );
			}
			break;
		case 0xcb:	/* RTC Data */
			if ( state->ws_portram[0xca] == 0x94 && state->rtc.index < 7 )
			{
				switch( state->rtc.index )
				{
				case 0:	state->rtc.year = data; break;
				case 1: state->rtc.month = data; break;
				case 2: state->rtc.day = data; break;
				case 3: state->rtc.day_of_week = data; break;
				case 4: state->rtc.hour = data; break;
				case 5: state->rtc.minute = data; break;
				case 6: state->rtc.second = data; break;
				}
				state->rtc.index++;
			}
			break;
		default:
			logerror( "Write to unsupported port: %X - %X\n", offset, data );
			break;
	}

	/* Update the port value */
	state->ws_portram[offset] = data;
}

static const char* wswan_determine_sram( wswan_state *state, UINT8 data )
{
	state->eeprom.write_enabled = 0;
	state->eeprom.mode = SRAM_UNKNOWN;
	switch( data )
	{
	case 0x00: state->eeprom.mode = SRAM_NONE; break;
	case 0x01: state->eeprom.mode = SRAM_64K; break;
	case 0x02: state->eeprom.mode = SRAM_256K; break;
	case 0x03: state->eeprom.mode = SRAM_1M; break;
	case 0x04: state->eeprom.mode = SRAM_2M; break;
	case 0x05: state->eeprom.mode = SRAM_512K; break;
	case 0x10: state->eeprom.mode = EEPROM_1K; break;
	case 0x20: state->eeprom.mode = EEPROM_16K; break;
	case 0x50: state->eeprom.mode = EEPROM_8K; break;
	}
	state->eeprom.size = wswan_sram_size[ state->eeprom.mode ];
	return wswan_sram_str[ state->eeprom.mode ];
}

enum enum_romsize { ROM_4M=0, ROM_8M, ROM_16M, ROM_32M, ROM_64M, ROM_128M, ROM_UNKNOWN };
static const char *const wswan_romsize_str[] = {
	"4Mbit", "8Mbit", "16Mbit", "32Mbit", "64Mbit", "128Mbit", "Unknown"
};

static const char* wswan_determine_romsize( UINT8 data )
{
	switch( data )
	{
	case 0x02:	return wswan_romsize_str[ ROM_4M ];
	case 0x03:	return wswan_romsize_str[ ROM_8M ];
	case 0x04:	return wswan_romsize_str[ ROM_16M ];
	case 0x06:	return wswan_romsize_str[ ROM_32M ];
	case 0x08:	return wswan_romsize_str[ ROM_64M ];
	case 0x09:	return wswan_romsize_str[ ROM_128M ];
	}
	return wswan_romsize_str[ ROM_UNKNOWN ];
}

DEVICE_START(wswan_cart)
{
	wswan_state *state = device->machine->driver_data<wswan_state>();
	/* Initialize EEPROM structure */
	memset( &state->eeprom, 0, sizeof( state->eeprom ) );
	state->eeprom.data = NULL;
	state->eeprom.page = NULL;

	/* Initialize RTC structure */
	state->rtc.present = 0;
	state->rtc.index = 0;
	state->rtc.year = 0;
	state->rtc.month = 0;
	state->rtc.day = 0;
	state->rtc.day_of_week = 0;
	state->rtc.hour = 0;
	state->rtc.minute = 0;
	state->rtc.second = 0;
	state->rtc.setting = 0xFF;
}

DEVICE_IMAGE_LOAD(wswan_cart)
{
	wswan_state *state = image.device().machine->driver_data<wswan_state>();
	UINT32 ii, size;
	const char *sram_str;

	if (image.software_entry() == NULL)
		size = image.length();
	else
		size = image.get_software_region_length("rom");

	state->ws_ram = (UINT8*) cputag_get_address_space(image.device().machine, "maincpu", ADDRESS_SPACE_PROGRAM)->get_read_ptr(0);
	memset(state->ws_ram, 0, 0xffff);
	state->ROMBanks = size / 65536;

	for (ii = 0; ii < state->ROMBanks; ii++)
	{
		if ((state->ROMMap[ii] = auto_alloc_array(image.device().machine, UINT8, 0x10000)))
		{
			if (image.software_entry() == NULL)
			{
				if (image.fread( state->ROMMap[ii], 0x10000) != 0x10000)
				{
					logerror("Error while reading loading rom!\n");
					return IMAGE_INIT_FAIL;
				}
			}
			else
				memcpy(state->ROMMap[ii], image.get_software_region("rom") + ii * 0x10000, 0x10000);
		}
		else
		{
			logerror("Memory allocation failed reading rom!\n");
			return IMAGE_INIT_FAIL;
		}
	}

	sram_str = wswan_determine_sram(state, state->ROMMap[state->ROMBanks - 1][0xfffb]);

	state->rtc.present = state->ROMMap[state->ROMBanks - 1][0xfffd] ? 1 : 0;

	{
		int sum = 0;
		/* Spit out some info */
		logerror("ROM DETAILS\n" );
		logerror("\tDeveloper ID: %X\n", state->ROMMap[state->ROMBanks - 1][0xfff6]);
		logerror("\tMinimum system: %s\n", state->ROMMap[state->ROMBanks - 1][0xfff7] ? "WonderSwan Color" : "WonderSwan");
		logerror("\tCart ID: %X\n", state->ROMMap[state->ROMBanks - 1][0xfff8]);
		logerror("\tROM size: %s\n", wswan_determine_romsize(state->ROMMap[state->ROMBanks - 1][0xfffa]));
		logerror("\tSRAM size: %s\n", sram_str);
		logerror("\tFeatures: %X\n", state->ROMMap[state->ROMBanks - 1][0xfffc]);
		logerror("\tRTC: %s\n", state->ROMMap[state->ROMBanks - 1][0xfffd] ? "yes" : "no");
		for (ii = 0; ii < state->ROMBanks; ii++)
		{
			int count;
			for (count = 0; count < 0x10000; count++)
			{
				sum += state->ROMMap[ii][count];
			}
		}
		sum -= state->ROMMap[state->ROMBanks - 1][0xffff];
		sum -= state->ROMMap[state->ROMBanks - 1][0xfffe];
		sum &= 0xffff;
		logerror("\tChecksum: %X%X (calculated: %04X)\n", state->ROMMap[state->ROMBanks - 1][0xffff], state->ROMMap[state->ROMBanks - 1][0xfffe], sum);
	}

	if (state->eeprom.size != 0)
	{
		state->eeprom.data = auto_alloc_array(image.device().machine, UINT8, state->eeprom.size);
		image.battery_load(state->eeprom.data, state->eeprom.size, 0x00);
		state->eeprom.page = state->eeprom.data;
	}

	if (image.software_entry() == NULL)
	{
		logerror("Image Name: %s\n", image.longname());
		logerror("Image Year: %s\n", image.year());
		logerror("Image Manufacturer: %s\n", image.manufacturer());
	}

	/* All done */
	return IMAGE_INIT_PASS;
}

static TIMER_CALLBACK(wswan_scanline_interrupt)
{
	wswan_state *state = machine->driver_data<wswan_state>();
	if( state->vdp.current_line < 144 )
	{
		wswan_refresh_scanline(machine);
	}

	/* Decrement 12kHz (HBlank) counter */
	if ( state->vdp.timer_hblank_enable && state->vdp.timer_hblank_reload != 0 )
	{
		state->vdp.timer_hblank_count--;
		logerror( "timer_hblank_count: %X\n", state->vdp.timer_hblank_count );
		if ( state->vdp.timer_hblank_count == 0 )
		{
			if ( state->vdp.timer_hblank_mode )
			{
				state->vdp.timer_hblank_count = state->vdp.timer_hblank_reload;
			}
			else
			{
				state->vdp.timer_hblank_reload = 0;
			}
			logerror( "trigerring hbltmr interrupt\n" );
			wswan_set_irq_line( machine, WSWAN_IFLAG_HBLTMR );
		}
	}

	/* Handle Sound DMA */
	if ( ( state->sound_dma.enable & 0x88 ) == 0x80 )
	{
		address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM );
		/* TODO: Output sound DMA byte */
		wswan_port_w( space, 0x89, space->read_byte(state->sound_dma.source ) );
		state->sound_dma.size--;
		state->sound_dma.source = ( state->sound_dma.source + 1 ) & 0x0FFFFF;
		if ( state->sound_dma.size == 0 )
		{
			state->sound_dma.enable &= 0x7F;
		}
	}

//  state->vdp.current_line = (state->vdp.current_line + 1) % 159;

	if( state->vdp.current_line == 144 )
	{
		wswan_set_irq_line( machine, WSWAN_IFLAG_VBL );
		/* Decrement 75Hz (VBlank) counter */
		if ( state->vdp.timer_vblank_enable && state->vdp.timer_vblank_reload != 0 )
		{
			state->vdp.timer_vblank_count--;
			logerror( "timer_vblank_count: %X\n", state->vdp.timer_vblank_count );
			if ( state->vdp.timer_vblank_count == 0 )
			{
				if ( state->vdp.timer_vblank_mode )
				{
					state->vdp.timer_vblank_count = state->vdp.timer_vblank_reload;
				}
				else
				{
					state->vdp.timer_vblank_reload = 0;
				}
				logerror( "triggering vbltmr interrupt\n" );
				wswan_set_irq_line( machine, WSWAN_IFLAG_VBLTMR );
			}
		}
	}

//  state->vdp.current_line = (state->vdp.current_line + 1) % 159;

	if ( state->vdp.current_line == state->vdp.line_compare )
	{
		wswan_set_irq_line( machine, WSWAN_IFLAG_LCMP );
	}

	state->vdp.current_line = (state->vdp.current_line + 1) % 159;

	if ( state->vdp.current_line == 0 )
	{
		if ( state->vdp.display_vertical != state->vdp.new_display_vertical )
		{
			state->vdp.display_vertical = state->vdp.new_display_vertical;
			if ( state->vdp.display_vertical )
			{
				machine->primary_screen->set_visible_area(5*8, 5*8 + WSWAN_Y_PIXELS - 1, 0, WSWAN_X_PIXELS - 1 );
			}
			else
			{
				machine->primary_screen->set_visible_area(0, WSWAN_X_PIXELS - 1, 5*8, 5*8 + WSWAN_Y_PIXELS - 1 );
			}
		}
	}
}

