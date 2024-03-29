/***************************************************************************

  gb.c

  Machine file to handle emulation of the Nintendo Game Boy.

  Changes:

    13/2/2002       AK - MBC2 and MBC3 support and added NVRAM support.
    23/2/2002       AK - MBC5 support, and MBC2 RAM support.
    13/3/2002       AK - Tidied up the MBC code, window layer now has it's
                         own palette. Tidied init code.
    15/3/2002       AK - More init code tidying with a slight hack to stop
                         sound when the machine starts.
    19/3/2002       AK - Changed NVRAM code to the new battery_* functions.
    24/3/2002       AK - Added MBC1 mode switching, and partial MBC3 RTC support.
    28/3/2002       AK - Improved LCD status timing and interrupts.
                         Free memory when we shutdown instead of leaking.
    31/3/2002       AK - Handle IO memory reading so we return 0xFF for registers
                         that are unsupported.
     7/4/2002       AK - Free memory from battery load/save. General tidying.
    13/4/2002       AK - Ok, don't free memory when we shutdown as that causes
                         a crash on reset.
    28/4/2002       AK - General code tidying.
                         Fixed MBC3's RAM/RTC banking.
                         Added support for games with more than 128 ROM banks.
    12/6/2002       AK - Rewrote the way bg and sprite palettes are handled.
                         The window layer no longer has it's own palette.
                         Added Super Game Boy support.
    13/6/2002       AK - Added Game Boy Color support.

    17/5/2004       WP - Added Megaduck/Cougar Boy support.
    13/6/2005       WP - Added support for bootstrap rom banking.

***************************************************************************/
#define __MACHINE_GB_C

#include "emu.h"
#include "cpu/lr35902/lr35902.h"
#include "imagedev/cartslot.h"
#include "machine/ram.h"
#include "image.h"
#include "audio/gb.h"
#include "includes/gb.h"

/* Memory bank controller types */
enum {
	MBC_NONE=0,		/*  32KB ROM - No memory bank controller         */
	MBC_MBC1,		/*  ~2MB ROM,   8KB RAM -or- 512KB ROM, 32KB RAM */
	MBC_MBC2,		/* 256KB ROM,  32KB RAM                          */
	MBC_MMM01,		/*    ?? ROM,    ?? RAM                          */
	MBC_MBC3,		/*   2MB ROM,  32KB RAM, RTC                     */
	MBC_MBC4,		/*    ?? ROM,    ?? RAM                          */
	MBC_MBC5,		/*   8MB ROM, 128KB RAM (32KB w/ Rumble)         */
	MBC_TAMA5,		/*    ?? ROM     ?? RAM - What is this?          */
	MBC_HUC1,		/*    ?? ROM,    ?? RAM - Hudson Soft Controller */
	MBC_HUC3,		/*    ?? ROM,    ?? RAM - Hudson Soft Controller */
	MBC_MBC6,		/*    ?? ROM,  32KB SRAM                         */
	MBC_MBC7,		/*    ?? ROM,    ?? RAM                          */
	MBC_WISDOM,		/*    ?? ROM,    ?? RAM - Wisdom tree controller */
	MBC_MBC1_KOR,	/*   1MB ROM,    ?? RAM - Korean MBC1 variant    */
	MBC_MEGADUCK,	/* MEGADUCK style banking                        */
	MBC_UNKNOWN,	/* Unknown mapper                                */
};

/* ram_get_ptr(machine->device(RAM_TAG)) layout defines */
#define CGB_START_VRAM_BANKS	0x0000
#define CGB_START_RAM_BANKS	( 2 * 8 * 1024 )

#define JOYPAD		state->gb_io[0x00]	/* Joystick: 1.1.P15.P14.P13.P12.P11.P10       */
#define SIODATA		state->gb_io[0x01]	/* Serial IO data buffer                       */
#define SIOCONT		state->gb_io[0x02]	/* Serial IO control register                  */
#define DIVREG		state->gb_io[0x04]	/* Divider register (???)                      */
#define TIMECNT		state->gb_io[0x05]	/* Timer counter. Gen. int. when it overflows  */
#define TIMEMOD		state->gb_io[0x06]	/* New value of TimeCount after it overflows   */
#define TIMEFRQ		state->gb_io[0x07]	/* Timer frequency and start/stop switch       */



/*
  Prototypes
*/

static TIMER_CALLBACK(gb_serial_timer_proc);
static void gb_machine_stop(running_machine &machine);
static WRITE8_HANDLER( gb_rom_bank_select_mbc1 );
static WRITE8_HANDLER( gb_ram_bank_select_mbc1 );
static WRITE8_HANDLER( gb_mem_mode_select_mbc1 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc2 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc3 );
static WRITE8_HANDLER( gb_ram_bank_select_mbc3 );
static WRITE8_HANDLER( gb_mem_mode_select_mbc3 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc5 );
static WRITE8_HANDLER( gb_ram_bank_select_mbc5 );
static WRITE8_HANDLER( gb_ram_bank_select_mbc6 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc6_1 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc6_2 );
static WRITE8_HANDLER( gb_rom_bank_select_mbc7 );
static WRITE8_HANDLER( gb_rom_bank_unknown_mbc7 );
static WRITE8_HANDLER( gb_ram_tama5 );
static WRITE8_HANDLER( gb_rom_bank_select_wisdom );
static WRITE8_HANDLER( gb_rom_bank_mmm01_0000_w );
static WRITE8_HANDLER( gb_rom_bank_mmm01_2000_w );
static WRITE8_HANDLER( gb_rom_bank_mmm01_4000_w );
static WRITE8_HANDLER( gb_rom_bank_mmm01_6000_w );
static WRITE8_HANDLER( gb_rom_bank_select_mbc1_kor );
static WRITE8_HANDLER( gb_ram_bank_select_mbc1_kor );
static WRITE8_HANDLER( gb_mem_mode_select_mbc1_kor );
static void gb_timer_increment( running_machine *machine );

#ifdef MAME_DEBUG
/* #define V_GENERAL*/		/* Display general debug information */
/* #define V_BANK*/			/* Display bank switching debug information */
#endif

static void gb_init_regs(running_machine *machine)
{
	gb_state *state = machine->driver_data<gb_state>();
	/* Initialize the registers */
	SIODATA = 0x00;
	SIOCONT = 0x7E;

	gb_io_w( cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM ), 0x05, 0x00 );		/* TIMECNT */
	gb_io_w( cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM ), 0x06, 0x00 );		/* TIMEMOD */
}

static void gb_rom16_0000( running_machine *machine, UINT8 *addr )
{
	memory_set_bankptr( machine, "bank5", addr );
	memory_set_bankptr( machine, "bank10", addr + 0x0100 );
	memory_set_bankptr( machine, "bank6", addr + 0x0200 );
	memory_set_bankptr( machine, "bank11", addr + 0x0900 );
}

static void gb_rom16_4000( running_machine *machine, UINT8 *addr )
{
	memory_set_bankptr( machine, "bank1", addr );
	memory_set_bankptr( machine, "bank4", addr + 0x2000 );
}

static void gb_rom8_4000( running_machine *machine, UINT8 *addr )
{
	memory_set_bankptr( machine, "bank1", addr );
}

static void gb_rom8_6000( running_machine *machine, UINT8 *addr )
{
	memory_set_bankptr( machine, "bank4", addr );
}

static void gb_init(running_machine *machine)
{
	gb_state *state = machine->driver_data<gb_state>();
	address_space *space = cputag_get_address_space( machine, "maincpu", ADDRESS_SPACE_PROGRAM );

	/* Initialize the memory banks */
	state->MBC1Mode = 0;
	state->MBC3RTCBank = 0;
	state->ROMBank = state->ROMBank00 + 1;
	state->RAMBank = 0;

	if (state->gb_cart)
	{
		if ( state->MBCType != MBC_MEGADUCK )
		{
			gb_rom16_4000( machine, state->ROMMap[state->ROMBank] );
			memory_set_bankptr (machine, "bank2", state->RAMMap[state->RAMBank] ? state->RAMMap[state->RAMBank] : state->gb_dummy_ram_bank);
		}
		else
		{
			memory_set_bankptr( machine, "bank1", state->ROMMap[state->ROMBank] );
			memory_set_bankptr( machine, "bank10", state->ROMMap[0] );
		}
	}

	/* Set handlers based on the Memory Bank Controller in the cart */
	switch( state->MBCType )
	{
		case MBC_NONE:
			break;
		case MBC_MMM01:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_rom_bank_mmm01_0000_w );
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_mmm01_2000_w);
			memory_install_write8_handler( space, 0x4000, 0x5fff, 0, 0, gb_rom_bank_mmm01_4000_w);
			memory_install_write8_handler( space, 0x6000, 0x7fff, 0, 0, gb_rom_bank_mmm01_6000_w);
			break;
		case MBC_MBC1:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_enable );	/* We don't emulate RAM enable yet */
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_select_mbc1 );
			memory_install_write8_handler( space, 0x4000, 0x5fff, 0, 0, gb_ram_bank_select_mbc1 );
			memory_install_write8_handler( space, 0x6000, 0x7fff, 0, 0, gb_mem_mode_select_mbc1 );
			break;
		case MBC_MBC2:
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_select_mbc2 );
			break;
		case MBC_MBC3:
		case MBC_HUC1:	/* Possibly wrong */
		case MBC_HUC3:	/* Possibly wrong */
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_enable );	/* We don't emulate RAM enable yet */
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_select_mbc3 );
			memory_install_write8_handler( space, 0x4000, 0x5fff, 0, 0, gb_ram_bank_select_mbc3 );
			memory_install_write8_handler( space, 0x6000, 0x7fff, 0, 0, gb_mem_mode_select_mbc3 );
			break;
		case MBC_MBC5:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_enable );
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_select_mbc5 );
			memory_install_write8_handler( space, 0x4000, 0x5fff, 0, 0, gb_ram_bank_select_mbc5 );
			break;
		case MBC_MBC6:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_bank_select_mbc6 );
			memory_install_write8_handler( space, 0x2000, 0x2fff, 0, 0, gb_rom_bank_select_mbc6_1 );
			memory_install_write8_handler( space, 0x3000, 0x3fff, 0, 0, gb_rom_bank_select_mbc6_2 );
			break;
		case MBC_MBC7:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_enable );
			memory_install_write8_handler( space, 0x2000, 0x2fff, 0, 0, gb_rom_bank_select_mbc7 );
			memory_install_write8_handler( space, 0x3000, 0x7fff, 0, 0, gb_rom_bank_unknown_mbc7 );
			break;
		case MBC_TAMA5:
			memory_install_write8_handler( space, 0xA000, 0xBFFF, 0, 0, gb_ram_tama5 );
			break;
		case MBC_WISDOM:
			memory_install_write8_handler( space, 0x0000, 0x3fff, 0, 0, gb_rom_bank_select_wisdom );
			break;
		case MBC_MBC1_KOR:
			memory_install_write8_handler( space, 0x0000, 0x1fff, 0, 0, gb_ram_enable ); /* We don't emulate RAM enable yet */
			memory_install_write8_handler( space, 0x2000, 0x3fff, 0, 0, gb_rom_bank_select_mbc1_kor );
			memory_install_write8_handler( space, 0x4000, 0x5fff, 0, 0, gb_ram_bank_select_mbc1_kor );
			memory_install_write8_handler( space, 0x6000, 0x7fff, 0, 0, gb_mem_mode_select_mbc1_kor );
			break;

		case MBC_MEGADUCK:
			memory_install_write8_handler( space, 0x0001, 0x0001, 0, 0, megaduck_rom_bank_select_type1 );
			memory_install_write8_handler( space, 0xB000, 0xB000, 0, 0, megaduck_rom_bank_select_type2 );
			break;
	}

	gb_sound_w(space->machine->device("custom"), 0x16, 0x00 );       /* Initialize sound hardware */

	state->divcount = 0;
	state->triggering_irq = 0;
	state->gb_io[0x07] = 0xF8;		/* Upper bits of TIMEFRQ register are set to 1 */
}

MACHINE_START( gb )
{
	gb_state *state = machine->driver_data<gb_state>();
	machine->add_notifier(MACHINE_NOTIFY_EXIT, gb_machine_stop);

	/* Allocate the serial timer, and disable it */
	state->gb_serial_timer = machine->scheduler().timer_alloc(FUNC(gb_serial_timer_proc));
	state->gb_serial_timer->enable( 0 );

	MACHINE_START_CALL( gb_video );
}

MACHINE_START( gbc )
{
	gb_state *state = machine->driver_data<gb_state>();
	machine->add_notifier(MACHINE_NOTIFY_EXIT, gb_machine_stop);

	/* Allocate the serial timer, and disable it */
	state->gb_serial_timer = machine->scheduler().timer_alloc(FUNC(gb_serial_timer_proc));
	state->gb_serial_timer->enable( 0 );

	MACHINE_START_CALL( gbc_video );
}

MACHINE_RESET( gb )
{
	gb_state *state = machine->driver_data<gb_state>();
	gb_init(machine);

	gb_video_reset( machine, GB_VIDEO_DMG );

	gb_rom16_0000( machine, state->ROMMap[state->ROMBank00] );

	/* Enable BIOS rom */
	memory_set_bankptr(machine, "bank5", machine->region("maincpu")->base() );

	state->divcount = 0x0004;
}


MACHINE_START( sgb )
{
	gb_state *state = machine->driver_data<gb_state>();

	state->sgb_packets = -1;
	state->sgb_tile_data = auto_alloc_array_clear(machine, UINT8, 0x2000 );

	machine->add_notifier(MACHINE_NOTIFY_EXIT, gb_machine_stop);

	/* Allocate the serial timer, and disable it */
	state->gb_serial_timer = machine->scheduler().timer_alloc(FUNC(gb_serial_timer_proc));
	state->gb_serial_timer->enable( 0 );

	MACHINE_START_CALL( gb_video );
}

MACHINE_RESET( sgb )
{
	gb_state *state = machine->driver_data<gb_state>();
	gb_init(machine);

	gb_video_reset( machine, GB_VIDEO_SGB );

	gb_init_regs(machine);

	gb_rom16_0000( machine, state->ROMMap[state->ROMBank00] ? state->ROMMap[state->ROMBank00] : state->gb_dummy_rom_bank );

	/* Enable BIOS rom */
	memory_set_bankptr(machine, "bank5", machine->region("maincpu")->base() );

	memset( state->sgb_tile_data, 0, 0x2000 );

	state->sgb_window_mask = 0;
	memset( state->sgb_pal_map, 0, sizeof(state->sgb_pal_map) );
	memset( state->sgb_atf_data, 0, sizeof(state->sgb_atf_data) );

	/* HACKS for Donkey Kong Land 2 + 3.
       For some reason that I haven't figured out, they store the tile
       data differently.  Hacks will go once I figure it out */
	state->sgb_hack = 0;

	if (state->gb_cart)	// make sure cart is in
	{
		if( strncmp( (const char*)(state->gb_cart + 0x134), "DONKEYKONGLAND 2", 16 ) == 0 ||
			strncmp( (const char*)(state->gb_cart + 0x134), "DONKEYKONGLAND 3", 16 ) == 0 )
				state->sgb_hack = 1;
	}

	state->divcount = 0x0004;
}

MACHINE_RESET( gbpocket )
{
	gb_state *state = machine->driver_data<gb_state>();
	gb_init(machine);

	gb_video_reset( machine, GB_VIDEO_MGB );

	gb_init_regs(machine);

	/* Initialize the Sound registers */
	gb_sound_w(machine->device("custom"), 0x16,0x80);
	gb_sound_w(machine->device("custom"), 0x15,0xF3);
	gb_sound_w(machine->device("custom"), 0x14,0x77);

	/* Enable BIOS rom if we have one */
	gb_rom16_0000( machine, state->ROMMap[state->ROMBank00] ? state->ROMMap[state->ROMBank00] : state->gb_dummy_rom_bank );

	state->divcount = 0xABC8;
}

MACHINE_RESET( gbc )
{
	gb_state *state = machine->driver_data<gb_state>();
	int ii;

	gb_init(machine);

	gb_video_reset( machine, GB_VIDEO_CGB );

	gb_init_regs(machine);

	gb_rom16_0000( machine, state->ROMMap[state->ROMBank00] ? state->ROMMap[state->ROMBank00] : state->gb_dummy_rom_bank );

	/* Enable BIOS rom */
	memory_set_bankptr(machine, "bank5", machine->region("maincpu")->base() );
	memory_set_bankptr(machine, "bank6", machine->region("maincpu")->base() + 0x100 );

	/* Allocate memory for internal ram */
	for( ii = 0; ii < 8; ii++ )
	{
		state->GBC_RAMMap[ii] = ram_get_ptr(machine->device(RAM_TAG)) + CGB_START_RAM_BANKS + ii * 0x1000;
		memset (state->GBC_RAMMap[ii], 0, 0x1000);
	}
}

static void gb_machine_stop(running_machine &machine)
{
	gb_state *state = machine.driver_data<gb_state>();
	/* Don't save if there was no battery */
	if(!(state->CartType & BATTERY) || !state->RAMBanks)
		return;

	/* NOTE: The reason we save the carts RAM this way instead of using MAME's
       built in macros is because they force the filename to be the name of
       the machine.  We need to have a separate name for each game. */
	device_image_interface *image = dynamic_cast<device_image_interface *>(machine.device("cart"));
	image->battery_save(state->gb_cart_ram, state->RAMBanks * 0x2000);
}

static void gb_set_mbc1_banks( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	gb_rom16_4000( machine, state->ROMMap[ state->ROMBank ] );
	memory_set_bankptr( machine, "bank2", state->RAMMap[ state->MBC1Mode ? ( state->ROMBank >> 5 ) : 0 ] );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc1 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	data &= 0x1F; /* Only uses lower 5 bits */
	/* Selecting bank 0 == selecting bank 1 */
	if( data == 0 )
		data = 1;

	state->ROMBank = ( state->ROMBank & 0x01E0 ) | data;
	/* Switch banks */
	gb_set_mbc1_banks( space->machine );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc2 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	data &= 0x0F; /* Only uses lower 4 bits */
	/* Selecting bank 0 == selecting bank 1 */
	if( data == 0 )
		data = 1;

	/* The least significant bit of the upper address byte must be 1 */
	if( offset & 0x0100 )
		state->ROMBank = ( state->ROMBank & 0x100 ) | data;
	/* Switch banks */
	gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc3 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write to mbc3 rom bank select register 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
	data &= 0x7F; /* Only uses lower 7 bits */
	/* Selecting bank 0 == selecting bank 1 */
	if( data == 0 )
		data = 1;

	state->ROMBank = ( state->ROMBank & 0x0100 ) | data;
	/* Switch banks */
	gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc5 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	/* MBC5 has a 9 bit bank select
      Writing into 2000-2FFF sets the lower 8 bits
      Writing into 3000-3FFF sets the 9th bit
    */
	logerror( "0x%04X: MBC5 ROM Bank select write 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
	if( offset & 0x1000 )
	{
		state->ROMBank = (state->ROMBank & 0xFF ) | ( ( data & 0x01 ) << 8 );
	}
	else
	{
		state->ROMBank = (state->ROMBank & 0x100 ) | data;
	}
	/* Switch banks */
	gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
}

static WRITE8_HANDLER( gb_ram_bank_select_mbc6 )
{
	logerror( "0x%04X: write to mbc6 ram enable area: %04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc6_1 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write to mbc6 rom area: 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), 0x2000 + offset, data );
	if ( offset & 0x0800 )
	{
		if ( data == 0x00 )
		{
			gb_rom8_4000( space->machine, state->ROMMap[state->ROMBank>>1] + ( ( state->ROMBank & 0x01 ) ? 0x2000 : 0x0000 ) );
		}
	}
	else
	{
		state->ROMBank = data;
	}
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc6_2 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write to mbc6 rom area: 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), 0x3000 + offset, data );
	if ( offset & 0x0800 )
	{
		if ( data == 0x00 )
		{
			gb_rom8_6000( space->machine, state->ROMMap[state->ROMBank00>>1] + ( ( state->ROMBank00 & 0x01 ) ? 0x2000 : 0x0000 ) );
		}
	}
	else
	{
		state->ROMBank00 = data;
	}
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc7 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write to mbc7 rom select register: 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), 0x2000 + offset, data );
	/* Bit 12 must be set for writing to the mbc register */
	if ( offset & 0x0100 )
	{
		state->ROMBank = data;
		gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
	}
}

static WRITE8_HANDLER( gb_rom_bank_unknown_mbc7 )
{
        logerror( "0x%04X: write to mbc7 rom area: 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), 0x3000 + offset, data );
	/* Bit 12 must be set for writing to the mbc register */
	if ( offset & 0x0100 )
	{
		switch( offset & 0x7000 )
		{
		case 0x0000:	/* 0x3000-0x3fff */
		case 0x1000:	/* 0x4000-0x4fff */
		case 0x2000:	/* 0x5000-0x5fff */
		case 0x3000:	/* 0x6000-0x6fff */
		case 0x4000:	/* 0x7000-0x7fff */
			break;
		}
	}
}

static WRITE8_HANDLER( gb_rom_bank_select_wisdom )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: wisdom tree mapper write to address 0x%04X\n", cpu_get_pc( space->cpu ), offset );
	/* The address determines the bank to select */
	state->ROMBank = ( offset << 1 ) & 0x1FF;
	memory_set_bankptr( space->machine, "bank5", state->ROMMap[ state->ROMBank ] );
	memory_set_bankptr( space->machine, "bank10", state->ROMMap[ state->ROMBank ] + 0x0100 );
	gb_rom16_4000( space->machine, state->ROMMap[ state->ROMBank + 1 ] );
}

static WRITE8_HANDLER( gb_ram_bank_select_mbc1 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	data &= 0x3; /* Only uses the lower 2 bits */

	/* Select the upper bits of the ROMMask */
	state->ROMBank = ( state->ROMBank & 0x1F ) | ( data << 5 );

	/* Switch banks */
	gb_set_mbc1_banks( space->machine );
}

static WRITE8_HANDLER( gb_ram_bank_select_mbc3 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write mbc3 ram bank select register 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
	if( data & 0x8 )
	{	/* RTC banks */
		if ( state->CartType & TIMER )
		{
			state->MBC3RTCBank = data & 0x07;
			if ( data < 5 )
			{
				memset( state->MBC3RTCData, state->MBC3RTCMap[state->MBC3RTCBank], 0x2000 );
				memory_set_bankptr( space->machine, "bank2", state->MBC3RTCData );
			}
		}
	}
	else
	{	/* RAM banks */
		state->RAMBank = data & 0x3;
		state->MBC3RTCBank = 0xFF;
		/* Switch banks */
		memory_set_bankptr( space->machine, "bank2", state->RAMMap[state->RAMBank] );
	}
}

static WRITE8_HANDLER( gb_ram_bank_select_mbc5 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: MBC5 RAM Bank select write 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
	data &= 0x0F;
	if( state->CartType & RUMBLE )
	{
		data &= 0x7;
	}
	state->RAMBank = data;
	/* Switch banks */
	memory_set_bankptr (space->machine, "bank2", state->RAMMap[state->RAMBank] );
}

WRITE8_HANDLER ( gb_ram_enable )
{
	/* FIXME: Currently we don't handle this, but a value of 0xA will enable
     * writing to the cart's RAM banks */
	logerror( "0x%04X: Write to ram enable register 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
}

static WRITE8_HANDLER( gb_mem_mode_select_mbc1 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	state->MBC1Mode = data & 0x1;
	gb_set_mbc1_banks( space->machine );
}

static WRITE8_HANDLER( gb_mem_mode_select_mbc3 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
        logerror( "0x%04X: Write to mbc3 mem mode select register 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), offset, data );
	if( state->CartType & TIMER )
	{
		/* FIXME: RTC Latch goes here */
		state->MBC3RTCMap[0] = 50;    /* Seconds */
		state->MBC3RTCMap[1] = 40;    /* Minutes */
		state->MBC3RTCMap[2] = 15;    /* Hours */
		state->MBC3RTCMap[3] = 25;    /* Day counter lowest 8 bits */
		state->MBC3RTCMap[4] = 0x01;  /* Day counter upper bit, timer off, no day overflow occured (bit7) */
	}
}

static WRITE8_HANDLER( gb_ram_tama5 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: TAMA5 write 0x%04X <- 0x%02X\n", cpu_get_pc( space->cpu ), 0xA000 + offset, data );
	switch( offset & 0x0001 )
	{
	case 0x0000:    /* Write to data register */
		switch( state->gbLastTama5Command )
		{
		case 0x00:      /* Bits 0-3 for rom bank selection */
			state->ROMBank = ( state->ROMBank & 0xF0 ) | ( data & 0x0F );
			gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
			break;
		case 0x01:      /* Bit 4(-7?) for rom bank selection */
			state->ROMBank = ( state->ROMBank & 0x0F ) | ( ( data & 0x0F ) << 4 );
			gb_rom16_4000( space->machine, state->ROMMap[state->ROMBank] );
			break;
		case 0x04:      /* Data to write lo */
			state->gbTama5Byte = ( state->gbTama5Byte & 0xF0 ) | ( data & 0x0F );
			break;
		case 0x05:      /* Data to write hi */
			state->gbTama5Byte = ( state->gbTama5Byte & 0x0F ) | ( ( data & 0x0F ) << 4 );
			break;
		case 0x06:      /* Address selection hi */
			state->gbTama5Address = ( state->gbTama5Address & 0x0F ) | ( ( data & 0x0F ) << 4 );
			break;
		case 0x07:      /* Address selection lo */
				/* This address always seems to written last, so we'll just
                   execute the command here */
			state->gbTama5Address = ( state->gbTama5Address & 0xF0 ) | ( data & 0x0F );
			switch ( state->gbTama5Address & 0xE0 )
			{
			case 0x00:      /* Write memory */
				logerror( "Write tama5 memory 0x%02X <- 0x%02X\n", state->gbTama5Address & 0x1F, state->gbTama5Byte );
				state->gbTama5Memory[ state->gbTama5Address & 0x1F ] = state->gbTama5Byte;
				break;
			case 0x20:      /* Read memory */
				logerror( "Read tama5 memory 0x%02X\n", state->gbTama5Address & 0x1F );
				state->gbTama5Byte = state->gbTama5Memory[ state->gbTama5Address & 0x1F ];
				break;
			case 0x40:      /* Unknown, some kind of read */
				if ( ( state->gbTama5Address & 0x1F ) == 0x12 )
				{
					state->gbTama5Byte = 0xFF;
				}
			case 0x80:      /* Unknown, some kind of read (when 07=01)/write (when 07=00/02) */
			default:
				logerror( "0x%04X: Unknown addressing mode\n", cpu_get_pc( space->cpu ) );
				break;
			}
			break;
		}
		break;
	case 0x0001:    /* Write to control register */
		switch( data )
		{
		case 0x00:      /* Bits 0-3 for rom bank selection */
		case 0x01:      /* Bits 4-7 for rom bank selection */
		case 0x04:      /* Data write register lo */
		case 0x05:      /* Data write register hi */
		case 0x06:      /* Address register hi */
		case 0x07:      /* Address register lo */
			break;
		case 0x0A:      /* Are we ready for the next command? */
			state->MBC3RTCData[0] = 0x01;
			memory_set_bankptr( space->machine, "bank2", state->MBC3RTCData );
			break;
		case 0x0C:      /* Data read register lo */
			state->MBC3RTCData[0] = state->gbTama5Byte & 0x0F;
			break;
		case 0x0D:      /* Data read register hi */
			state->MBC3RTCData[0] = ( state->gbTama5Byte & 0xF0 ) >> 4;
			break;
		default:
			logerror( "0x%04X: Unknown tama5 command 0x%02X\n", cpu_get_pc( space->cpu ), data );
			break;
		}
		state->gbLastTama5Command = data;
		break;
	}
}

/* This mmm01 implementation is mostly guess work, no clue how correct it all is */


static WRITE8_HANDLER( gb_rom_bank_mmm01_0000_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write 0x%02X to 0x%04X\n", cpu_get_pc( space->cpu ), data, offset+0x000 );
	if ( data & 0x40 )
	{
		state->mmm01_bank_offset = state->mmm01_reg1;
		memory_set_bankptr( space->machine, "bank5", state->ROMMap[ state->mmm01_bank_offset ] );
		memory_set_bankptr( space->machine, "bank10", state->ROMMap[ state->mmm01_bank_offset ] + 0x0100 );
		gb_rom16_4000( space->machine, state->ROMMap[ state->mmm01_bank_offset + state->mmm01_bank ] );
	}
}

static WRITE8_HANDLER( gb_rom_bank_mmm01_2000_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write 0x%02X to 0x%04X\n", cpu_get_pc( space->cpu ), data, offset+0x2000 );

	state->mmm01_reg1 = data & state->ROMMask;
	state->mmm01_bank = state->mmm01_reg1 & state->mmm01_bank_mask;
	if ( state->mmm01_bank == 0 )
	{
		state->mmm01_bank = 1;
	}
	gb_rom16_4000( space->machine, state->ROMMap[ state->mmm01_bank_offset + state->mmm01_bank ] );
}

static WRITE8_HANDLER( gb_rom_bank_mmm01_4000_w )
{
	logerror( "0x%04X: write 0x%02X to 0x%04X\n", cpu_get_pc( space->cpu ), data, offset+0x4000 );
}

static WRITE8_HANDLER( gb_rom_bank_mmm01_6000_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	logerror( "0x%04X: write 0x%02X to 0x%04X\n", cpu_get_pc( space->cpu ), data, offset+0x6000 );
	/* Not sure if this is correct, Taito Variety Pack sets these values */
	/* Momotarou Collection 2 writes 01 and 21 here */
	switch( data )
	{
	case 0x30:	state->mmm01_bank_mask = 0x07;	break;
	case 0x38:	state->mmm01_bank_mask = 0x03;	break;
	default:	state->mmm01_bank_mask = 0xFF; break;
	}
}

/* Korean MBC1 variant mapping */

static void gb_set_mbc1_kor_banks( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	if ( state->ROMBank & 0x30 )
	{
		gb_rom16_0000( machine, state->ROMMap[ state->ROMBank & 0x30 ] );
	}
	gb_rom16_4000( machine, state->ROMMap[ state->ROMBank ] );
	memory_set_bankptr( machine, "bank2", state->RAMMap[ state->MBC1Mode ? ( state->ROMBank >> 5 ) : 0 ] );
}

static WRITE8_HANDLER( gb_rom_bank_select_mbc1_kor )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	data &= 0x0F; /* Only uses lower 5 bits */
	/* Selecting bank 0 == selecting bank 1 */
	if( data == 0 )
		data = 1;

	state->ROMBank = ( state->ROMBank & 0x01F0 ) | data;
	/* Switch banks */
	gb_set_mbc1_kor_banks( space->machine );
}

static WRITE8_HANDLER( gb_ram_bank_select_mbc1_kor )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	data &= 0x3; /* Only uses the lower 2 bits */

	/* Select the upper bits of the ROMMask */
	state->ROMBank = ( state->ROMBank & 0x0F ) | ( data << 4 );

	/* Switch banks */
	gb_set_mbc1_kor_banks( space->machine );
}

static WRITE8_HANDLER( gb_mem_mode_select_mbc1_kor )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	state->MBC1Mode = data & 0x1;
	gb_set_mbc1_kor_banks( space->machine );
}

WRITE8_HANDLER ( gb_io_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	static const UINT8 timer_shifts[4] = {10, 4, 6, 8};

	switch (offset)
	{
	case 0x00:						/* JOYP - Joypad */
		JOYPAD = 0xCF | data;
		if (!(data & 0x20))
			JOYPAD &= (input_port_read(space->machine, "INPUTS") >> 4) | 0xF0;
		if (!(data & 0x10))
			JOYPAD &= input_port_read(space->machine, "INPUTS") | 0xF0;
		return;
	case 0x01:						/* SB - Serial transfer data */
		break;
	case 0x02:						/* SC - SIO control */
		switch( data & 0x81 )
		{
		case 0x00:
		case 0x01:
		case 0x80:				/* enabled & external clock */
			state->SIOCount = 0;
			break;
		case 0x81:				/* enabled & internal clock */
			SIODATA = 0xFF;
			state->SIOCount = 8;
			state->gb_serial_timer->adjust(space->machine->device<cpu_device>("maincpu")->cycles_to_attotime(512), 0, space->machine->device<cpu_device>("maincpu")->cycles_to_attotime(512));
			state->gb_serial_timer->enable( 1 );
			break;
		}
		break;
	case 0x04:						/* DIV - Divider register */
		/* Force increment of TIMECNT register */
		if ( state->divcount >= 16 )
			gb_timer_increment(space->machine);
		state->divcount = 0;
		return;
	case 0x05:						/* TIMA - Timer counter */
		/* Check if the counter is being reloaded in this cycle */
		if ( state->reloading && ( state->divcount & ( state->shift_cycles - 1 ) ) == 4 )
		{
			data = TIMECNT;
		}
		break;
	case 0x06:						/* TMA - Timer module */
		/* Check if the counter is being reloaded in this cycle */
		if ( state->reloading && ( state->divcount & ( state->shift_cycles - 1 ) ) == 4 )
		{
			TIMECNT = data;
		}
		break;
	case 0x07:						/* TAC - Timer control */
		data |= 0xF8;
		/* Check if timer is just disabled or the timer frequency is changing */
		if ( ( ! ( data & 0x04 ) && ( TIMEFRQ & 0x04 ) ) || ( ( data & 0x04 ) && ( TIMEFRQ & 0x04 ) && ( data & 0x03 ) != ( TIMEFRQ & 0x03 ) ) )
		{
			/* Check if TIMECNT should be incremented */
			if ( ( state->divcount & ( state->shift_cycles - 1 ) ) >= ( state->shift_cycles >> 1 ) )
			{
				gb_timer_increment(space->machine);
			}
		}
		state->shift = timer_shifts[data & 0x03];
		state->shift_cycles = 1 << state->shift;
		break;
	case 0x0F:						/* IF - Interrupt flag */
		data &= 0x1F;
		cpu_set_reg( space->machine->device("maincpu"), LR35902_IF, data );
		break;
	}

	state->gb_io[offset] = data;
}

WRITE8_HANDLER ( gb_io2_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	if ( offset == 0x10 )
	{
		/* disable BIOS ROM */
		gb_rom16_0000( space->machine, state->ROMMap[state->ROMBank00] );
	}
	else
	{
		gb_video_w( space, offset, data );
	}
}

#ifdef MAME_DEBUG
static const char *const sgbcmds[26] =
{
	"PAL01   ",
	"PAL23   ",
	"PAL03   ",
	"PAL12   ",
	"ATTR_BLK",
	"ATTR_LIN",
	"ATTR_DIV",
	"ATTR_CHR",
	"SOUND   ",
	"SOU_TRN ",
	"PAL_SET ",
	"PAL_TRN ",
	"ATRC_EN ",
	"TEST_EN ",
	"ICON_EN ",
	"DATA_SND",
	"DATA_TRN",
	"MLT_REG ",
	"JUMP    ",
	"CHR_TRN ",
	"PCT_TRN ",
	"ATTR_TRN",
	"ATTR_SET",
	"MASK_EN ",
	"OBJ_TRN ",
	"????????",
};
#endif

WRITE8_HANDLER ( sgb_io_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	UINT8 *sgb_data = state->sgb_data;

	switch( offset )
	{
		case 0x00:
			switch (data & 0x30)
			{
			case 0x00:				   /* start condition */
				if (state->sgb_start)
					logerror("SGB: Start condition before end of transfer ??\n");
				state->sgb_bitcount = 0;
				state->sgb_start = 1;
				state->sgb_rest = 0;
				JOYPAD = 0x0F & ((input_port_read(space->machine, "INPUTS") >> 4) | input_port_read(space->machine, "INPUTS") | 0xF0);
				break;
			case 0x10:				   /* data true */
				if (state->sgb_rest)
				{
					/* We should test for this case , but the code below won't
                       work with the current setup */
#if 0
					if (state->sgb_bytecount == 16)
					{
						logerror("SGB: end of block is not zero!");
						state->sgb_start = 0;
					}
#endif
					sgb_data[state->sgb_bytecount] >>= 1;
					sgb_data[state->sgb_bytecount] |= 0x80;
					state->sgb_bitcount++;
					if (state->sgb_bitcount == 8)
					{
						state->sgb_bitcount = 0;
						state->sgb_bytecount++;
					}
					state->sgb_rest = 0;
				}
				JOYPAD = 0x1F & ((input_port_read(space->machine, "INPUTS") >> 4) | 0xF0);
				break;
			case 0x20:				/* data false */
				if (state->sgb_rest)
				{
					if( state->sgb_bytecount == 16 && state->sgb_packets == -1 )
					{
#ifdef MAME_DEBUG
						logerror("SGB: %s (%02X) pkts: %d data: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
								sgbcmds[sgb_data[0] >> 3],sgb_data[0] >> 3, sgb_data[0] & 0x07, sgb_data[1], sgb_data[2], sgb_data[3],
								sgb_data[4], sgb_data[5], sgb_data[6], sgb_data[7],
								sgb_data[8], sgb_data[9], sgb_data[10], sgb_data[11],
								sgb_data[12], sgb_data[13], sgb_data[14], sgb_data[15]);
#endif
						state->sgb_packets = sgb_data[0] & 0x07;
						state->sgb_start = 0;
					}
					if (state->sgb_bytecount == (state->sgb_packets << 4) )
					{
						switch( sgb_data[0] >> 3 )
						{
							case 0x00:	/* PAL01 */
								state->sgb_pal[0*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[0*4 + 1] = sgb_data[3] | (sgb_data[4] << 8);
								state->sgb_pal[0*4 + 2] = sgb_data[5] | (sgb_data[6] << 8);
								state->sgb_pal[0*4 + 3] = sgb_data[7] | (sgb_data[8] << 8);
								state->sgb_pal[1*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[1*4 + 1] = sgb_data[9] | (sgb_data[10] << 8);
								state->sgb_pal[1*4 + 2] = sgb_data[11] | (sgb_data[12] << 8);
								state->sgb_pal[1*4 + 3] = sgb_data[13] | (sgb_data[14] << 8);
								break;
							case 0x01:	/* PAL23 */
								state->sgb_pal[2*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[2*4 + 1] = sgb_data[3] | (sgb_data[4] << 8);
								state->sgb_pal[2*4 + 2] = sgb_data[5] | (sgb_data[6] << 8);
								state->sgb_pal[2*4 + 3] = sgb_data[7] | (sgb_data[8] << 8);
								state->sgb_pal[3*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[3*4 + 1] = sgb_data[9] | (sgb_data[10] << 8);
								state->sgb_pal[3*4 + 2] = sgb_data[11] | (sgb_data[12] << 8);
								state->sgb_pal[3*4 + 3] = sgb_data[13] | (sgb_data[14] << 8);
								break;
							case 0x02:	/* PAL03 */
								state->sgb_pal[0*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[0*4 + 1] = sgb_data[3] | (sgb_data[4] << 8);
								state->sgb_pal[0*4 + 2] = sgb_data[5] | (sgb_data[6] << 8);
								state->sgb_pal[0*4 + 3] = sgb_data[7] | (sgb_data[8] << 8);
								state->sgb_pal[3*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[3*4 + 1] = sgb_data[9] | (sgb_data[10] << 8);
								state->sgb_pal[3*4 + 2] = sgb_data[11] | (sgb_data[12] << 8);
								state->sgb_pal[3*4 + 3] = sgb_data[13] | (sgb_data[14] << 8);
								break;
							case 0x03:	/* PAL12 */
								state->sgb_pal[1*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[1*4 + 1] = sgb_data[3] | (sgb_data[4] << 8);
								state->sgb_pal[1*4 + 2] = sgb_data[5] | (sgb_data[6] << 8);
								state->sgb_pal[1*4 + 3] = sgb_data[7] | (sgb_data[8] << 8);
								state->sgb_pal[2*4 + 0] = sgb_data[1] | (sgb_data[2] << 8);
								state->sgb_pal[2*4 + 1] = sgb_data[9] | (sgb_data[10] << 8);
								state->sgb_pal[2*4 + 2] = sgb_data[11] | (sgb_data[12] << 8);
								state->sgb_pal[2*4 + 3] = sgb_data[13] | (sgb_data[14] << 8);
								break;
							case 0x04:	/* ATTR_BLK */
								{
									UINT8 I, J, K, o;
									for( K = 0; K < sgb_data[1]; K++ )
									{
										o = K * 6;
										if( sgb_data[o + 2] & 0x1 )
										{
											for( I = sgb_data[ o + 4]; I <= sgb_data[o + 6]; I++ )
											{
												for( J = sgb_data[o + 5]; J <= sgb_data[o + 7]; J++ )
												{
													state->sgb_pal_map[I][J] = sgb_data[o + 3] & 0x3;
												}
											}
										}
									}
								}
								break;
							case 0x05:	/* ATTR_LIN */
								{
									UINT8 J, K;
									if( sgb_data[1] > 15 )
										sgb_data[1] = 15;
									for( K = 0; K < sgb_data[1]; K++ )
									{
										if( sgb_data[K + 1] & 0x80 )
										{
											for( J = 0; J < 20; J++ )
											{
												state->sgb_pal_map[J][sgb_data[K + 1] & 0x1f] = (sgb_data[K + 1] & 0x60) >> 5;
											}
										}
										else
										{
											for( J = 0; J < 18; J++ )
											{
												state->sgb_pal_map[sgb_data[K + 1] & 0x1f][J] = (sgb_data[K + 1] & 0x60) >> 5;
											}
										}
									}
								}
								break;
							case 0x06:	/* ATTR_DIV */
								{
									UINT8 I, J;
									if( sgb_data[1] & 0x40 ) /* Vertical */
									{
										for( I = 0; I < sgb_data[2]; I++ )
										{
											for( J = 0; J < 20; J++ )
											{
												state->sgb_pal_map[J][I] = (sgb_data[1] & 0xC) >> 2;
											}
										}
										for( J = 0; J < 20; J++ )
										{
											state->sgb_pal_map[J][sgb_data[2]] = (sgb_data[1] & 0x30) >> 4;
										}
										for( I = sgb_data[2] + 1; I < 18; I++ )
										{
											for( J = 0; J < 20; J++ )
											{
												state->sgb_pal_map[J][I] = sgb_data[1] & 0x3;
											}
										}
									}
									else /* Horizontal */
									{
										for( I = 0; I < sgb_data[2]; I++ )
										{
											for( J = 0; J < 18; J++ )
											{
												state->sgb_pal_map[I][J] = (sgb_data[1] & 0xC) >> 2;
											}
										}
										for( J = 0; J < 18; J++ )
										{
											state->sgb_pal_map[sgb_data[2]][J] = (sgb_data[1] & 0x30) >> 4;
										}
										for( I = sgb_data[2] + 1; I < 20; I++ )
										{
											for( J = 0; J < 18; J++ )
											{
												state->sgb_pal_map[I][J] = sgb_data[1] & 0x3;
											}
										}
									}
								}
								break;
							case 0x07:	/* ATTR_CHR */
								{
									UINT16 I, sets;
									UINT8 x, y;
									sets = (sgb_data[3] | (sgb_data[4] << 8) );
									if( sets > 360 )
										sets = 360;
									sets >>= 2;
									sets += 6;
									x = sgb_data[1];
									y = sgb_data[2];
									if( sgb_data[5] ) /* Vertical */
									{
										for( I = 6; I < sets; I++ )
										{
											state->sgb_pal_map[x][y++] = (sgb_data[I] & 0xC0) >> 6;
											if( y > 17 )
											{
												y = 0;
												x++;
												if( x > 19 )
													x = 0;
											}

											state->sgb_pal_map[x][y++] = (sgb_data[I] & 0x30) >> 4;
											if( y > 17 )
											{
												y = 0;
												x++;
												if( x > 19 )
													x = 0;
											}

											state->sgb_pal_map[x][y++] = (sgb_data[I] & 0xC) >> 2;
											if( y > 17 )
											{
												y = 0;
												x++;
												if( x > 19 )
													x = 0;
											}

											state->sgb_pal_map[x][y++] = sgb_data[I] & 0x3;
											if( y > 17 )
											{
												y = 0;
												x++;
												if( x > 19 )
													x = 0;
											}
										}
									}
									else /* horizontal */
									{
										for( I = 6; I < sets; I++ )
										{
											state->sgb_pal_map[x++][y] = (sgb_data[I] & 0xC0) >> 6;
											if( x > 19 )
											{
												x = 0;
												y++;
												if( y > 17 )
													y = 0;
											}

											state->sgb_pal_map[x++][y] = (sgb_data[I] & 0x30) >> 4;
											if( x > 19 )
											{
												x = 0;
												y++;
												if( y > 17 )
													y = 0;
											}

											state->sgb_pal_map[x++][y] = (sgb_data[I] & 0xC) >> 2;
											if( x > 19 )
											{
												x = 0;
												y++;
												if( y > 17 )
													y = 0;
											}

											state->sgb_pal_map[x++][y] = sgb_data[I] & 0x3;
											if( x > 19 )
											{
												x = 0;
												y++;
												if( y > 17 )
													y = 0;
											}
										}
									}
								}
								break;
							case 0x08:	/* SOUND */
								/* This command enables internal sound effects */
								/* Not Implemented */
								break;
							case 0x09:	/* SOU_TRN */
								/* This command sends data to the SNES sound processor.
                                   We'll need to emulate that for this to be used */
								/* Not Implemented */
								break;
							case 0x0A:	/* PAL_SET */
								{
									UINT16 index_, J, I;

									/* Palette 0 */
									index_ = (UINT16)(sgb_data[1] | (sgb_data[2] << 8)) * 4;
									state->sgb_pal[0] = state->sgb_pal_data[index_];
									state->sgb_pal[1] = state->sgb_pal_data[index_ + 1];
									state->sgb_pal[2] = state->sgb_pal_data[index_ + 2];
									state->sgb_pal[3] = state->sgb_pal_data[index_ + 3];
									/* Palette 1 */
									index_ = (UINT16)(sgb_data[3] | (sgb_data[4] << 8)) * 4;
									state->sgb_pal[4] = state->sgb_pal_data[index_];
									state->sgb_pal[5] = state->sgb_pal_data[index_ + 1];
									state->sgb_pal[6] = state->sgb_pal_data[index_ + 2];
									state->sgb_pal[7] = state->sgb_pal_data[index_ + 3];
									/* Palette 2 */
									index_ = (UINT16)(sgb_data[5] | (sgb_data[6] << 8)) * 4;
									state->sgb_pal[8] = state->sgb_pal_data[index_];
									state->sgb_pal[9] = state->sgb_pal_data[index_ + 1];
									state->sgb_pal[10] = state->sgb_pal_data[index_ + 2];
									state->sgb_pal[11] = state->sgb_pal_data[index_ + 3];
									/* Palette 3 */
									index_ = (UINT16)(sgb_data[7] | (sgb_data[8] << 8)) * 4;
									state->sgb_pal[12] = state->sgb_pal_data[index_];
									state->sgb_pal[13] = state->sgb_pal_data[index_ + 1];
									state->sgb_pal[14] = state->sgb_pal_data[index_ + 2];
									state->sgb_pal[15] = state->sgb_pal_data[index_ + 3];
									/* Attribute File */
									if( sgb_data[9] & 0x40 )
										state->sgb_window_mask = 0;
									state->sgb_atf = (sgb_data[9] & 0x3f) * (18 * 5);
									if( sgb_data[9] & 0x80 )
									{
										for( J = 0; J < 18; J++ )
										{
											for( I = 0; I < 5; I++ )
											{
												state->sgb_pal_map[I * 4][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0xC0) >> 6;
												state->sgb_pal_map[(I * 4) + 1][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0x30) >> 4;
												state->sgb_pal_map[(I * 4) + 2][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0xC) >> 2;
												state->sgb_pal_map[(I * 4) + 3][J] = state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0x3;
											}
										}
									}
								}
								break;
							case 0x0B:	/* PAL_TRN */
								{
									UINT8 *gb_vram = gb_get_vram_ptr( space->machine );
									UINT16 I, col;

									for( I = 0; I < 2048; I++ )
									{
										col = ( gb_vram[ 0x0800 + (I*2) + 1 ] << 8 ) | gb_vram[ 0x0800 + (I*2) ];
										state->sgb_pal_data[I] = col;
									}
								}
								break;
							case 0x0C:	/* ATRC_EN */
								/* Not Implemented */
								break;
							case 0x0D:	/* TEST_EN */
								/* Not Implemented */
								break;
							case 0x0E:	/* ICON_EN */
								/* Not Implemented */
								break;
							case 0x0F:	/* DATA_SND */
								/* Not Implemented */
								break;
							case 0x10:	/* DATA_TRN */
								/* Not Implemented */
								break;
							case 0x11:	/* MLT_REQ - Multi controller request */
								if (sgb_data[1] == 0x00)
									state->sgb_controller_mode = 0;
								else if (sgb_data[1] == 0x01)
									state->sgb_controller_mode = 2;
								break;
							case 0x12:	/* JUMP */
								/* Not Implemented */
								break;
							case 0x13:	/* CHR_TRN */
								if( sgb_data[1] & 0x1 )
									memcpy( state->sgb_tile_data + 4096, gb_get_vram_ptr(space->machine) + 0x0800, 4096 );
								else
									memcpy( state->sgb_tile_data, gb_get_vram_ptr(space->machine) + 0x0800, 4096 );
								break;
							case 0x14:	/* PCT_TRN */
								{
									int I;
									UINT16 col;
									UINT8 *gb_vram = gb_get_vram_ptr( space->machine );
									if( state->sgb_hack )
									{
										memcpy( state->sgb_tile_map, gb_vram + 0x1000, 2048 );
										for( I = 0; I < 64; I++ )
										{
											col = ( gb_vram[ 0x0800 + (I*2) + 1 ] << 8 ) | gb_vram[ 0x0800 + (I*2) ];
											state->sgb_pal[SGB_BORDER_PAL_OFFSET + I] = col;
										}
									}
									else /* Do things normally */
									{
										memcpy( state->sgb_tile_map, gb_vram + 0x0800, 2048 );
										for( I = 0; I < 64; I++ )
										{
											col = ( gb_vram[ 0x1000 + (I*2) + 1 ] << 8 ) | gb_vram[ 0x1000 + (I*2) ];
											state->sgb_pal[SGB_BORDER_PAL_OFFSET + I] = col;
										}
									}
								}
								break;
							case 0x15:	/* ATTR_TRN */
								memcpy( state->sgb_atf_data, gb_get_vram_ptr( space->machine ) + 0x0800, 4050 );
								break;
							case 0x16:	/* ATTR_SET */
								{
									UINT8 J, I;

									/* Attribute File */
									if( sgb_data[1] & 0x40 )
										state->sgb_window_mask = 0;
									state->sgb_atf = (sgb_data[1] & 0x3f) * (18 * 5);
									for( J = 0; J < 18; J++ )
									{
										for( I = 0; I < 5; I++ )
										{
											state->sgb_pal_map[I * 4][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0xC0) >> 6;
											state->sgb_pal_map[(I * 4) + 1][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0x30) >> 4;
											state->sgb_pal_map[(I * 4) + 2][J] = (state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0xC) >> 2;
											state->sgb_pal_map[(I * 4) + 3][J] = state->sgb_atf_data[(J * 5) + state->sgb_atf + I] & 0x3;
										}
									}
								}
								break;
							case 0x17:	/* MASK_EN */
								state->sgb_window_mask = sgb_data[1];
								break;
							case 0x18:	/* OBJ_TRN */
								/* Not Implemnted */
								break;
							case 0x19:	/* ? */
								/* Called by: dkl,dkl2,dkl3,zeldadx
                                   But I don't know what it is for. */
								/* Not Implemented */
								break;
							case 0x1E:	/* Used by bootrom to transfer the gb cart header */
								break;
							case 0x1F:	/* Used by bootrom to transfer the gb cart header */
								break;
							default:
								logerror( "SGB: Unknown Command 0x%02x!\n", sgb_data[0] >> 3 );
						}

						state->sgb_start = 0;
						state->sgb_bytecount = 0;
						state->sgb_packets = -1;
					}
					if( state->sgb_start )
					{
						sgb_data[state->sgb_bytecount] >>= 1;
						state->sgb_bitcount++;
						if (state->sgb_bitcount == 8)
						{
							state->sgb_bitcount = 0;
							state->sgb_bytecount++;
						}
					}
					state->sgb_rest = 0;
				}
				JOYPAD = 0x2F & (input_port_read(space->machine, "INPUTS") | 0xF0);
				break;
			case 0x30:				   /* rest condition */
				if (state->sgb_start)
					state->sgb_rest = 1;
				if (state->sgb_controller_mode)
				{
					state->sgb_controller_no++;
					if (state->sgb_controller_no == state->sgb_controller_mode)
						state->sgb_controller_no = 0;
					JOYPAD = 0x3F - state->sgb_controller_no;
				}
				else
					JOYPAD = 0x3F;

				/* Hack to let cartridge know it's running on an SGB */
				if ( (sgb_data[0] >> 3) == 0x1F )
					JOYPAD = 0x3E;
				break;
			}
			return;
		default:
			/* we didn't handle the write, so pass it to the GB handler */
			gb_io_w( space, offset, data );
			return;
	}

	state->gb_io[offset] = data;
}

/* Interrupt Enable register */
READ8_HANDLER( gb_ie_r )
{
	return cpu_get_reg( space->machine->device("maincpu"), LR35902_IE );
}

WRITE8_HANDLER ( gb_ie_w )
{
	cpu_set_reg( space->machine->device("maincpu"), LR35902_IE, data & 0x1F );
}

/* IO read */
READ8_HANDLER ( gb_io_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch(offset)
	{
		case 0x04:
			return ( state->divcount >> 8 ) & 0xFF;
		case 0x00:
		case 0x01:
		case 0x02:
		case 0x03:
		case 0x05:
		case 0x06:
		case 0x07:
			return state->gb_io[offset];
		case 0x0F:
			/* Make sure the internal states are up to date */
			return 0xE0 | cpu_get_reg( space->machine->device("maincpu"), LR35902_IF );
		default:
			/* It seems unsupported registers return 0xFF */
			return 0xFF;
	}
}

DEVICE_START(gb_cart)
{
	gb_state *state = device->machine->driver_data<gb_state>();
	int I;

	state->gb_dummy_rom_bank = auto_alloc_array(device->machine, UINT8, 0x4000);
	memset(state->gb_dummy_rom_bank, 0xff, 0x4000);

	state->gb_dummy_ram_bank = auto_alloc_array(device->machine, UINT8, 0x2000);
	memset(state->gb_dummy_ram_bank, 0xff, 0x2000 );

	for(I = 0; I < MAX_ROMBANK; I++)
	{
		state->ROMMap[I] = state->gb_dummy_rom_bank;
	}
	for(I = 0; I < MAX_RAMBANK; I++)
	{
		state->RAMMap[I] = state->gb_dummy_ram_bank;
	}
	state->ROMBank00 = 0;
	state->ROMBanks = 0;
	state->RAMBanks = 0;
	state->MBCType = MBC_NONE;
	state->CartType = 0;
	state->ROMMask = 0;
	state->RAMMask = 0;
}

DEVICE_IMAGE_LOAD(gb_cart)
{
	gb_state *state = image.device().machine->driver_data<gb_state>();
	static const char *const CartTypes[] =
	{
		"ROM ONLY",
		"ROM+MBC1",
		"ROM+MBC1+RAM",
		"ROM+MBC1+RAM+BATTERY",
        "UNKNOWN",
		"ROM+MBC2",
		"ROM+MBC2+BATTERY",
        "UNKNOWN",
		"ROM+RAM",
		"ROM+RAM+BATTERY",
        "UNKNOWN",
		"ROM+MMM01",
		"ROM+MMM01+SRAM",
		"ROM+MMM01+SRAM+BATTERY",
        "UNKNOWN",
		"ROM+MBC3+TIMER+BATTERY",
		"ROM+MBC3+TIMER+RAM+BATTERY",
		"ROM+MBC3",
		"ROM+MBC3+RAM",
		"ROM+MBC3+RAM+BATTERY",
        "UNKNOWN",
        "UNKNOWN",
        "UNKNOWN",
        "UNKNOWN",
        "UNKNOWN",
		"ROM+MBC5",
		"ROM+MBC5+RAM",
		"ROM+MBC5+RAM+BATTERY",
		"ROM+MBC5+RUMBLE",
		"ROM+MBC5+RUMBLE+SRAM",
		"ROM+MBC5+RUMBLE+SRAM+BATTERY",
		"Pocket Camera",
		"Bandai TAMA5",
		/* Need heaps of unknowns here */
		"Hudson HuC-3",
		"Hudson HuC-1"
	};

/*** Following are some known manufacturer codes *************************/
	static const struct
	{
		UINT16 Code;
		const char *Name;
	}
	Companies[] =
	{
		{0x3301, "Nintendo"},
		{0x7901, "Accolade"},
		{0xA400, "Konami"},
		{0x6701, "Ocean"},
		{0x5601, "LJN"},
		{0x9900, "ARC?"},
		{0x0101, "Nintendo"},
		{0x0801, "Capcom"},
		{0x0100, "Nintendo"},
		{0xBB01, "SunSoft"},
		{0xA401, "Konami"},
		{0xAF01, "Namcot?"},
		{0x4901, "Irem"},
		{0x9C01, "Imagineer"},
		{0xA600, "Kawada?"},
		{0xB101, "Nexoft"},
		{0x5101, "Acclaim"},
		{0x6001, "Titus"},
		{0xB601, "HAL"},
		{0x3300, "Nintendo"},
		{0x0B00, "Coconuts?"},
		{0x5401, "Gametek"},
		{0x7F01, "Kemco?"},
		{0xC001, "Taito"},
		{0xEB01, "Atlus"},
		{0xE800, "Asmik?"},
		{0xDA00, "Tomy?"},
		{0xB100, "ASCII?"},
		{0xEB00, "Atlus"},
		{0xC000, "Taito"},
		{0x9C00, "Imagineer"},
		{0xC201, "Kemco?"},
		{0xD101, "Sofel?"},
		{0x6101, "Virgin"},
		{0xBB00, "SunSoft"},
		{0xCE01, "FCI?"},
		{0xB400, "Enix?"},
		{0xBD01, "Imagesoft"},
		{0x0A01, "Jaleco?"},
		{0xDF00, "Altron?"},
		{0xA700, "Takara?"},
		{0xEE00, "IGS?"},
		{0x8300, "Lozc?"},
		{0x5001, "Absolute?"},
		{0xDD00, "NCS?"},
		{0xE500, "Epoch?"},
		{0xCB00, "VAP?"},
		{0x8C00, "Vic Tokai"},
		{0xC200, "Kemco?"},
		{0xBF00, "Sammy?"},
		{0x1800, "Hudson Soft"},
		{0xCA01, "Palcom/Ultra"},
		{0xCA00, "Palcom/Ultra"},
		{0xC500, "Data East?"},
		{0xA900, "Technos Japan?"},
		{0xD900, "Banpresto?"},
		{0x7201, "Broderbund?"},
		{0x7A01, "Triffix Entertainment?"},
		{0xE100, "Towachiki?"},
		{0x9300, "Tsuburava?"},
		{0xC600, "Tonkin House?"},
		{0xCE00, "Pony Canyon"},
		{0x7001, "Infogrames?"},
		{0x8B01, "Bullet-Proof Software?"},
		{0x5501, "Park Place?"},
		{0xEA00, "King Records?"},
		{0x5D01, "Tradewest?"},
		{0x6F01, "ElectroBrain?"},
		{0xAA01, "Broderbund?"},
		{0xC301, "SquareSoft"},
		{0x5201, "Activision?"},
		{0x5A01, "Bitmap Brothers/Mindscape"},
		{0x5301, "American Sammy"},
		{0x4701, "Spectrum Holobyte"},
		{0x1801, "Hudson Soft"},
		{0x0000, NULL}
	};

	int Checksum, I, J, filesize, load_start = 0;
	UINT16 reported_rom_banks;
	UINT8 *gb_header;
	static const int rambanks[8] = {0, 1, 1, 4, 16, 8, 0, 0};

	if (image.software_entry() == NULL)
		filesize = image.length();
	else
		filesize = image.get_software_region_length("rom");

	/* Check for presence of a header, and skip that header */
	J = filesize % 0x4000;
	if (J == 512)
	{
		logerror("Rom-header found, skipping\n");
		load_start = 512;
		filesize -= 512;
	}

	/* Verify that the file contains 16kb blocks */
	if ((filesize == 0) || ((filesize % 0x4000) != 0))
	{
		image.seterror(IMAGE_ERROR_UNSPECIFIED, "Invalid rom file size");
		return IMAGE_INIT_FAIL;
	}

	/* Claim memory */
	state->gb_cart = auto_alloc_array(image.device().machine, UINT8, filesize);

	if (image.software_entry() == NULL)
	{
		/* Actually skip the header */
		image.fseek(load_start, SEEK_SET);

		/* Read cartridge */
		if (image.fread( state->gb_cart, filesize) != filesize)
		{
			image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unable to fully read from file");
			return IMAGE_INIT_FAIL;
		}
	}
	else
		memcpy(state->gb_cart, image.get_software_region("rom") + load_start, filesize);

	gb_header = state->gb_cart;
	state->ROMBank00 = 0;

	/* Check for presence of MMM01 mapper */
	if (filesize >= 0x8000)
	{
		static const UINT8 nintendo_logo[0x18] = {
			0xCE, 0xED, 0x66, 0x66, 0xCC, 0x0D, 0x00, 0x0B,
			0x03, 0x73, 0x00, 0x83, 0x00, 0x0C, 0x00, 0x0D,
			0x00, 0x08, 0x11, 0x1F, 0x88, 0x89, 0x00, 0x0E
		};
		int	bytes_matched = 0;
		gb_header = state->gb_cart + filesize - 0x8000;
		for (I = 0; I < 0x18; I++)
		{
			if (gb_header[0x0104 + I] == nintendo_logo[I])
			{
				bytes_matched++;
			}
		}

		if (bytes_matched == 0x18 && gb_header[0x0147] >= 0x0B && gb_header[0x0147] <= 0x0D)
		{
			state->ROMBank00 = (filesize / 0x4000) - 2;
			state->mmm01_bank_offset = state->ROMBank00;
		}
		else
		{
			gb_header = state->gb_cart;
		}
	}

	/* Fill in our cart details */
	switch(gb_header[0x0147])
	{
	case 0x00:	state->MBCType = MBC_NONE;	state->CartType = 0;				break;
	case 0x01:	state->MBCType = MBC_MBC1;	state->CartType = 0;				break;
	case 0x02:	state->MBCType = MBC_MBC1;	state->CartType = CART_RAM;				break;
	case 0x03:	state->MBCType = MBC_MBC1;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x05:	state->MBCType = MBC_MBC2;	state->CartType = 0;				break;
	case 0x06:	state->MBCType = MBC_MBC2;	state->CartType = BATTERY;			break;
	case 0x08:	state->MBCType = MBC_NONE;	state->CartType = CART_RAM;				break;
	case 0x09:	state->MBCType = MBC_NONE;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x0B:	state->MBCType = MBC_MMM01;	state->CartType = 0;				break;
	case 0x0C:	state->MBCType = MBC_MMM01;	state->CartType = CART_RAM;				break;
	case 0x0D:	state->MBCType = MBC_MMM01;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x0F:	state->MBCType = MBC_MBC3;	state->CartType = TIMER | BATTERY;		break;
	case 0x10:	state->MBCType = MBC_MBC3;	state->CartType = TIMER | CART_RAM | BATTERY;	break;
	case 0x11:	state->MBCType = MBC_MBC3;	state->CartType = 0;				break;
	case 0x12:	state->MBCType = MBC_MBC3;	state->CartType = CART_RAM;				break;
	case 0x13:	state->MBCType = MBC_MBC3;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x15:	state->MBCType = MBC_MBC4;	state->CartType = 0;				break;
	case 0x16:	state->MBCType = MBC_MBC4;	state->CartType = CART_RAM;				break;
	case 0x17:	state->MBCType = MBC_MBC4;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x19:	state->MBCType = MBC_MBC5;	state->CartType = 0;				break;
	case 0x1A:	state->MBCType = MBC_MBC5;	state->CartType = CART_RAM;				break;
	case 0x1B:	state->MBCType = MBC_MBC5;	state->CartType = CART_RAM | BATTERY;		break;
	case 0x1C:	state->MBCType = MBC_MBC5;	state->CartType = RUMBLE;			break;
	case 0x1D:	state->MBCType = MBC_MBC5;	state->CartType = RUMBLE | SRAM;		break;
	case 0x1E:	state->MBCType = MBC_MBC5;	state->CartType = RUMBLE | SRAM | BATTERY;	break;
	case 0x20:	state->MBCType = MBC_MBC6;	state->CartType = SRAM; break;
	case 0x22:	state->MBCType = MBC_MBC7;	state->CartType = SRAM | BATTERY;		break;
	case 0xBE:	state->MBCType = MBC_NONE;	state->CartType = 0;				break;	/* used in Flash2Advance GB Bridge boot program */
	case 0xFD:	state->MBCType = MBC_TAMA5;	state->CartType = 0 /*RTC | BATTERY?*/;	break;
	case 0xFE:	state->MBCType = MBC_HUC3;	state->CartType = 0;				break;
	case 0xFF:	state->MBCType = MBC_HUC1;	state->CartType = 0;				break;
	default:	state->MBCType = MBC_UNKNOWN;	state->CartType = UNKNOWN;			break;
	}

	/* Check whether we're dealing with a (possible) Wisdom Tree game here */
	if (gb_header[0x0147] == 0x00)
	{
		int count = 0;
		for (I = 0x0134; I <= 0x014C; I++)
		{
			count += gb_header[I];
		}
		if (count == 0)
		{
			state->MBCType = MBC_WISDOM;
		}
	}

	/* Check if we're dealing with a Korean variant of the MBC1 mapper */
	if (state->MBCType == MBC_MBC1)
	{
		if (gb_header[0x13F] == 0x42 && gb_header[0x140] == 0x32 && gb_header[0x141] == 0x43 && gb_header[0x142] == 0x4B)
		{
			state->MBCType = MBC_MBC1_KOR;
		}
	}
	if (state->MBCType == MBC_UNKNOWN)
	{
		image.seterror(IMAGE_ERROR_UNSUPPORTED, "Unknown mapper type");
		return IMAGE_INIT_FAIL;
	}
	if (state->MBCType == MBC_MMM01)
	{
//      image.seterror(IMAGE_ERROR_UNSUPPORTED, "Mapper MMM01 is not supported yet");
//      return IMAGE_INIT_FAIL;
	}
	if (state->MBCType == MBC_MBC4)
	{
		image.seterror(IMAGE_ERROR_UNSUPPORTED, "Mapper MBC4 is not supported yet");
		return IMAGE_INIT_FAIL;
	}
	/* MBC7 support is still work-in-progress, so only enable it for debug builds */
#ifndef MAME_DEBUG
	if (state->MBCType == MBC_MBC7)
	{
		image.seterror(IMAGE_ERROR_UNSUPPORTED, "Mapper MBC7 is not supported yet");
		return IMAGE_INIT_FAIL;
	}
#endif

	state->ROMBanks = filesize / 0x4000;
	switch (gb_header[0x0148])
	{
	case 0x52:
		reported_rom_banks = 72;
		break;
	case 0x53:
		reported_rom_banks = 80;
		break;
	case 0x54:
		reported_rom_banks = 96;
		break;
	case 0x00: case 0x01: case 0x02: case 0x03:
	case 0x04: case 0x05: case 0x06: case 0x07:
		reported_rom_banks = 2 << gb_header[0x0148];
		break;
	default:
		logerror("Warning loading cartridge: Unknown ROM size in header.\n");
		reported_rom_banks = 256;
		break;
	}
	if (state->ROMBanks != reported_rom_banks && state->MBCType != MBC_WISDOM)
	{
		logerror("Warning loading cartridge: Filesize and reported ROM banks don't match.\n");
	}

	state->RAMBanks = rambanks[gb_header[0x0149] & 7];

	/* Calculate and check checksum */
	Checksum = ((UINT16) gb_header[0x014E] << 8) + gb_header[0x014F];
	Checksum += gb_header[0x014E] + gb_header[0x014F];
	for (I = 0; I < filesize; I++)
	{
		Checksum -= state->gb_cart[I];
	}
	if (Checksum & 0xFFFF)
	{
		logerror("Warning loading cartridge: Checksum is wrong.");
	}

	/* Initialize ROMMap pointers */
	for (I = 0; I < state->ROMBanks; I++)
	{
		state->ROMMap[I] = state->gb_cart + (I * 0x4000);
	}

	/*
      Handle odd-sized cartridges (72,80,96 banks)
      ROMBanks      ROMMask
      72 (1001000)  1000111 (71)
      80 (1010000)  1001111 (79)
      96 (1100000)  1011111 (95)
    */
	state->ROMMask = I - 1;
	if ((state->ROMBanks & state->ROMMask) != 0)
	{
		for( ; I & state->ROMBanks; I++)
		{
			state->ROMMap[I] = state->ROMMap[I & state->ROMMask];
		}
		state->ROMMask = I - 1;
	}

	/* Fill out the remaining rom bank pointers, if any. */
	for ( ; I < MAX_ROMBANK; I++)
	{
		state->ROMMap[I] = state->ROMMap[I & state->ROMMask];
	}

	/* Log cart information */
	{
		const char *P;
		char S[50];
		static const int ramsize[8] = { 0, 2, 8, 32, 128, 64, 0, 0 };


		strncpy (S, (char *)&gb_header[0x0134], 16);
		S[16] = '\0';
		logerror("Cart Information\n");
		logerror("\tName:             %s\n", S);
		logerror("\tType:             %s [0x%2X]\n", CartTypes[gb_header[0x0147]], gb_header[0x0147] );
		logerror("\tGame Boy:         %s\n", (gb_header[0x0143] == 0xc0) ? "No" : "Yes" );
		logerror("\tSuper GB:         %s [0x%2X]\n", (gb_header[0x0146] == 0x03) ? "Yes" : "No", gb_header[0x0146] );
		logerror("\tColor GB:         %s [0x%2X]\n", (gb_header[0x0143] == 0x80 || gb_header[0x0143] == 0xc0) ? "Yes" : "No", state->gb_cart[0x0143] );
		logerror("\tROM Size:         %d 16kB Banks [0x%2X]\n", state->ROMBanks, gb_header[0x0148]);
		logerror("\tRAM Size:         %d kB [0x%2X]\n", ramsize[ gb_header[0x0149] & 0x07 ], gb_header[0x0149]);
		logerror("\tLicense code:     0x%2X%2X\n", gb_header[0x0145], gb_header[0x0144] );
		J = ((UINT16) gb_header[0x014B] << 8) + gb_header[0x014A];
		for (I = 0, P = NULL; !P && Companies[I].Name; I++)
			if (J == Companies[I].Code)
				P = Companies[I].Name;
		logerror("\tManufacturer ID:  0x%2X", J);
		logerror(" [%s]\n", P ? P : "?");
		logerror("\tVersion Number:   0x%2X\n", gb_header[0x014C]);
		logerror("\tComplement Check: 0x%2X\n", gb_header[0x014D]);
		logerror("\tChecksum:         0x%2X\n", ( ( gb_header[0x014E] << 8 ) + gb_header[0x014F] ) );
		J = ((UINT16) gb_header[0x0103] << 8) + gb_header[0x0102];
		logerror("\tStart Address:    0x%2X\n", J);
	}

	/* MBC2 has 512 * 4bits (8kb) internal RAM */
	if(state->MBCType == MBC_MBC2)
		state->RAMBanks = 1;
	/* MBC7 has 512 bytes(?) of internal RAM */
	if (state->MBCType == MBC_MBC7)
	{
		state->RAMBanks = 1;
	}

	if (state->RAMBanks && state->MBCType)
	{
		/* Claim memory */
		state->gb_cart_ram = auto_alloc_array(image.device().machine, UINT8, state->RAMBanks * 0x2000);
		memset(state->gb_cart_ram, 0xFF, state->RAMBank * 0x2000);

		for (I = 0; I < state->RAMBanks; I++)
		{
			state->RAMMap[I] = state->gb_cart_ram + (I * 0x2000);
		}

		/* Set up rest of the (mirrored) RAM pages */
		state->RAMMask = I - 1;
		for ( ; I < MAX_RAMBANK; I++)
		{
			state->RAMMap[I] = state->RAMMap[I & state->RAMMask];
		}
	}
	else
	{
		state->RAMMask = 0;
	}

	/* If there's an RTC claim memory to store the RTC contents */
	if (state->CartType & TIMER)
	{
		state->MBC3RTCData = auto_alloc_array(image.device().machine, UINT8, 0x2000);
	}

	if (state->MBCType == MBC_TAMA5)
	{
		state->MBC3RTCData = auto_alloc_array(image.device().machine, UINT8, 0x2000);
		memset(state->gbTama5Memory, 0xff, sizeof(state->gbTama5Memory));
	}

	/* Load the saved RAM if this cart has a battery */
	if (state->CartType & BATTERY && state->RAMBanks)
	{
		image.battery_load(state->gb_cart_ram, state->RAMBanks * 0x2000, 0x00);
	}

	return IMAGE_INIT_PASS;
}

INTERRUPT_GEN( gb_scanline_interrupt )
{
}

static TIMER_CALLBACK(gb_serial_timer_proc)
{
	gb_state *state = machine->driver_data<gb_state>();
	/* Shift in a received bit */
	SIODATA = (SIODATA << 1) | 0x01;
	/* Decrement number of handled bits */
	state->SIOCount--;
	/* If all bits done, stop timer and trigger interrupt */
	if ( ! state->SIOCount )
	{
		SIOCONT &= 0x7F;
		state->gb_serial_timer->enable( 0 );
		cputag_set_input_line(machine, "maincpu", SIO_INT, ASSERT_LINE);
	}
}

INLINE void gb_timer_check_irq( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	state->reloading = 0;
	if ( state->triggering_irq )
	{
		state->triggering_irq = 0;
		if ( TIMECNT == 0 )
		{
			TIMECNT = TIMEMOD;
			cputag_set_input_line(machine, "maincpu", TIM_INT, ASSERT_LINE );
			state->reloading = 1;
		}
	}
}

static void gb_timer_increment( running_machine *machine )
{
	gb_state *state = machine->driver_data<gb_state>();
	gb_timer_check_irq(machine);

	TIMECNT += 1;
	if ( TIMECNT == 0 )
	{
		state->triggering_irq = 1;
	}
}

void gb_timer_callback(device_t *device, int cycles)
{
	gb_state *state = device->machine->driver_data<gb_state>();
	UINT16 old_gb_divcount = state->divcount;
	state->divcount += cycles;

	gb_timer_check_irq(device->machine);

	if ( TIMEFRQ & 0x04 )
	{
		UINT16 old_count = old_gb_divcount >> state->shift;
		UINT16 new_count = state->divcount >> state->shift;
		if ( cycles > state->shift_cycles )
		{
			gb_timer_increment(device->machine);
			old_count++;
		}
		if ( new_count != old_count )
		{
			gb_timer_increment(device->machine);
		}
		if ( new_count << state->shift < state->divcount )
		{
			gb_timer_check_irq(device->machine);
		}
	}
}

WRITE8_HANDLER ( gbc_io2_w )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch( offset )
	{
		case 0x0D:	/* KEY1 - Prepare speed switch */
			cpu_set_reg( space->machine->device("maincpu"), LR35902_SPEED, data );
			return;
		case 0x10:	/* BFF - Bios disable */
			gb_rom16_0000( space->machine, state->ROMMap[state->ROMBank00] );
			return;
		case 0x16:	/* RP - Infrared port */
			break;
		case 0x30:	/* SVBK - RAM bank select */
			state->GBC_RAMBank = data & 0x7;
			if ( ! state->GBC_RAMBank )
				state->GBC_RAMBank = 1;
			memory_set_bankptr (space->machine, "bank3", state->GBC_RAMMap[state->GBC_RAMBank]);
			break;
		default:
			break;
	}
	gbc_video_w( space, offset, data );
}

READ8_HANDLER( gbc_io2_r )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	switch( offset )
	{
	case 0x0D:	/* KEY1 */
		return cpu_get_reg( space->machine->device("maincpu"), LR35902_SPEED );
	case 0x16:	/* RP - Infrared port */
		break;
	case 0x30:	/* SVBK - RAM bank select */
		return state->GBC_RAMBank;
	default:
		break;
	}
	return gbc_video_r( space, offset );
}

/****************************************************************************

  Megaduck routines

 ****************************************************************************/

MACHINE_START( megaduck )
{
	gb_state *state = machine->driver_data<gb_state>();
	/* Allocate the serial timer, and disable it */
	state->gb_serial_timer = machine->scheduler().timer_alloc(FUNC(gb_serial_timer_proc));
	state->gb_serial_timer->enable( 0 );

	MACHINE_START_CALL( gb_video );
}

MACHINE_RESET( megaduck )
{
	/* We may have to add some more stuff here, if not then it can be merged back into gb */
	gb_init(machine);

	gb_video_reset( machine, GB_VIDEO_DMG );
}

/*
 Map megaduck video related area on to regular Game Boy video area

 Different locations of the video registers:
 Register      Game Boy   MegaDuck
 LCDC          FF40       FF10  (See different bit order below)
 STAT          FF41       FF11
 SCY           FF42       FF12
 SCX           FF43       FF13
 LY            FF44       FF18
 LYC           FF45       FF19
 DMA           FF46       FF1A
 BGP           FF47       FF1B
 OBP0          FF48       FF14
 OBP1          FF49       FF15
 WY            FF4A       FF16
 WX            FF4B       FF17
 Unused        FF4C       FF4C (?)
 Unused        FF4D       FF4D (?)
 Unused        FF4E       FF4E (?)
 Unused        FF4F       FF4F (?)

 Different LCDC register

 Game Boy       MegaDuck
 0                      6       - BG & Window Display : 0 - Off, 1 - On
 1                      0       - OBJ Display: 0 - Off, 1 - On
 2                      1       - OBJ Size: 0 - 8x8, 1 - 8x16
 3                      2       - BG Tile Map Display: 0 - 9800, 1 - 9C00
 4                      4       - BG & Window Tile Data Select: 0 - 8800, 1 - 8000
 5                      5       - Window Display: 0 - Off, 1 - On
 6                      3       - Window Tile Map Display Select: 0 - 9800, 1 - 9C00
 7                      7       - LCD Operation

 **************/

 READ8_HANDLER( megaduck_video_r )
{
	UINT8 data;

	if ( (offset & 0x0C) && ((offset & 0x0C) ^ 0x0C) )
	{
		offset ^= 0x0C;
	}
	data = gb_video_r( space, offset );
	if ( offset )
		return data;
	return BITSWAP8(data,7,0,5,4,6,3,2,1);
}

WRITE8_HANDLER ( megaduck_video_w )
{
	if ( !offset )
	{
		data = BITSWAP8(data,7,3,5,4,2,1,0,6);
	}
	if ( (offset & 0x0C) && ((offset & 0x0C) ^ 0x0C) )
	{
		offset ^= 0x0C;
	}
	gb_video_w(space, offset, data );
}

/* Map megaduck audio offset to game boy audio offsets */

static const UINT8 megaduck_sound_offsets[16] = { 0, 2, 1, 3, 4, 6, 5, 7, 8, 9, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };

WRITE8_HANDLER( megaduck_sound_w1 )
{
	gb_sound_w(space->machine->device("custom"), megaduck_sound_offsets[offset], data );
}

READ8_HANDLER( megaduck_sound_r1 )
{
	return gb_sound_r( space->machine->device("custom"), megaduck_sound_offsets[offset] );
}

WRITE8_HANDLER( megaduck_sound_w2 )
{
	switch(offset)
	{
		case 0x00:	gb_sound_w(space->machine->device("custom"), 0x10, data );	break;
		case 0x01:	gb_sound_w(space->machine->device("custom"), 0x12, data );	break;
		case 0x02:	gb_sound_w(space->machine->device("custom"), 0x11, data );	break;
		case 0x03:	gb_sound_w(space->machine->device("custom"), 0x13, data );	break;
		case 0x04:	gb_sound_w(space->machine->device("custom"), 0x14, data );	break;
		case 0x05:	gb_sound_w(space->machine->device("custom"), 0x16, data );	break;
		case 0x06:	gb_sound_w(space->machine->device("custom"), 0x15, data );	break;
		case 0x07:
		case 0x08:
		case 0x09:
		case 0x0A:
		case 0x0B:
		case 0x0C:
		case 0x0D:
		case 0x0E:
		case 0x0F:
			break;
	}
}

READ8_HANDLER( megaduck_sound_r2 )
{
	return gb_sound_r(space->machine->device("custom"), 0x10 + megaduck_sound_offsets[offset]);
}

WRITE8_HANDLER( megaduck_rom_bank_select_type1 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	if( state->ROMMask )
	{
		state->ROMBank = data & state->ROMMask;

		/* Switch banks */
		memory_set_bankptr (space->machine, "bank1", state->ROMMap[state->ROMBank]);
	}
}

WRITE8_HANDLER( megaduck_rom_bank_select_type2 )
{
	gb_state *state = space->machine->driver_data<gb_state>();
	if( state->ROMMask )
	{
		state->ROMBank = (data << 1) & state->ROMMask;

		/* Switch banks */
		memory_set_bankptr( space->machine, "bank10", state->ROMMap[state->ROMBank]);
		memory_set_bankptr( space->machine, "bank1", state->ROMMap[state->ROMBank + 1]);
	}
}

DEVICE_IMAGE_LOAD(megaduck_cart)
{
	gb_state *state = image.device().machine->driver_data<gb_state>();
	int I;
	UINT32 filesize;

	for (I = 0; I < MAX_ROMBANK; I++)
		state->ROMMap[I] = NULL;
	for (I = 0; I < MAX_RAMBANK; I++)
		state->RAMMap[I] = NULL;

	if (image.software_entry() == NULL)
		filesize = image.length();
	else
		filesize = image.get_software_region_length("rom");

	if ((filesize == 0) || ((filesize % 0x4000) != 0))
	{
		image.seterror(IMAGE_ERROR_UNSPECIFIED, "Invalid rom file size");
		return IMAGE_INIT_FAIL;
	}

	state->ROMBanks = filesize / 0x4000;

	/* Claim memory */
	state->gb_cart = auto_alloc_array(image.device().machine, UINT8, filesize);

	/* Read cartridge */
	if (image.software_entry() == NULL)
	{
		if (image.fread( state->gb_cart, filesize) != filesize)
		{
			image.seterror(IMAGE_ERROR_UNSPECIFIED, "Unable to fully read from file");
			return IMAGE_INIT_FAIL;
		}
	}
	else
	{
		memcpy(state->gb_cart, image.get_software_region("rom"), filesize);
	}

	/* Log cart information */
	{
		logerror("Cart Information\n");
		logerror("\tRom Banks:        %d\n", state->ROMBanks);
	}

	for (I = 0; I < state->ROMBanks; I++)
	{
		state->ROMMap[I] = state->gb_cart + (I * 0x4000);
	}

	/* Build rom bank Mask */
	if (state->ROMBanks < 3)
		state->ROMMask = 0;
	else
	{
		for (I = 1; I < state->ROMBanks; I <<= 1) ;
			state->ROMMask = I - 1;
	}

	state->MBCType = MBC_MEGADUCK;

	return IMAGE_INIT_PASS;
}
