/***************************************************************************

    Sega System 16C hardware

****************************************************************************/

#include "emu.h"
#include "cpu/z80/z80.h"
#include "cpu/m68000/m68000.h"
#include "cpu/mcs51/mcs51.h"
#include "includes/segas16.h"
#include "machine/segaic16.h"
#include "sound/2151intf.h"
#include "sound/upd7759.h"
#include "video/segaic16.h"


/*************************************
 *
 *  Constants
 *
 *************************************/

#define MASTER_CLOCK_10MHz		XTAL_10MHz
#define MASTER_CLOCK_8MHz		XTAL_8MHz
#define MASTER_CLOCK_25MHz		XTAL_25_1748MHz


/*************************************
 *
 *  Statics
 *
 *************************************/

static UINT16 *workram;


/*************************************
 *
 *  Prototypes
 *
 *************************************/

static READ16_HANDLER( misc_io_r );
static WRITE16_HANDLER( misc_io_w );
static WRITE16_HANDLER( rom_5704_bank_w );

/*************************************
 *
 *  Memory mapping tables
 *
 *************************************/

static const segaic16_memory_map_entry rom_5704_custom_info[] =
{
	{ 0x3d/2, 0x00000, 0x04000, 0xffc000,      ~0, misc_io_r, NULL,     misc_io_w,             NULL,     NULL,                  "I/O space" },
	{ 0x39/2, 0x00000, 0x01000, 0xfff000,      ~0, NULL,      "bank10", segaic16_paletteram_w, NULL,     &segaic16_paletteram,  "color RAM" },
	{ 0x35/2, 0x00000, 0x10000, 0xfe0000,      ~0, NULL,      "bank11", segaic16_tileram_0_w,  NULL,     &segaic16_tileram_0,   "tile RAM" },
	{ 0x35/2, 0x10000, 0x01000, 0xfef000,      ~0, NULL,      "bank12", segaic16_textram_0_w,  NULL,     &segaic16_textram_0,   "text RAM" },
	{ 0x31/2, 0x00000, 0x02000, 0xffe000,      ~0, NULL,      "bank13", NULL,                  "bank13", &segaic16_spriteram_0, "object RAM" },
	{ 0x2d/2, 0x00000, 0x40000, 0xfc0000,      ~0, NULL,      "bank14", NULL,                  "bank14", &workram,				"work RAM" },
	{ 0x29/2, 0x00000, 0x10000, 0xff0000,      ~0, NULL,      NULL,     rom_5704_bank_w,       NULL,     NULL,                  "tile bank" },
	{ 0x25/2, 0x00000, 0x80000, 0xfc0000, 0x80000, NULL,      "bank16", NULL,                  NULL,     NULL,                  "ROM 1" },
	{ 0x21/2, 0x00000, 0x80000, 0xfc0000, 0x00000, NULL,      "bank17", NULL,                  NULL,     NULL,                  "ROM 0" },
	{ 0 }
};

/*************************************
 *
 *  Configuration
 *
 *************************************/

static void sound_w(running_machine *machine, UINT8 data)
{
	segas1x_state *state = machine->driver_data<segas1x_state>();

	if (state->soundcpu != NULL)
	{
		address_space *space = cpu_get_address_space(state->maincpu, ADDRESS_SPACE_PROGRAM);
		soundlatch_w(space, 0, data & 0xff);
		cpu_set_input_line(state->soundcpu, 0, HOLD_LINE);
	}
}

static void system16c_generic_init(running_machine *machine)
{
	segas1x_state *state = machine->driver_data<segas1x_state>();

	/* allocate memory for regions not autmatically assigned */
	segaic16_spriteram_0 = auto_alloc_array(machine, UINT16, 0x02000 / 2);
	segaic16_paletteram  = auto_alloc_array(machine, UINT16, 0x01000 / 2);
	segaic16_tileram_0   = auto_alloc_array(machine, UINT16, 0x10000 / 2);
	segaic16_textram_0   = auto_alloc_array(machine, UINT16, 0x01000 / 2);
	workram              = auto_alloc_array(machine, UINT16, 0x40000 / 2);

	/* init the memory mapper */
	segaic16_memory_mapper_init(machine->device("maincpu"), rom_5704_custom_info, sound_w, NULL);

	/* reset the custom handlers and other pointers */
	state->custom_io_r = NULL;
	state->custom_io_w = NULL;
	state->i8751_vblank_hook = NULL;
	state->i8751_initial_config = NULL;
	state->disable_screen_blanking = 0;

	state->maincpu = machine->device("maincpu");
	state->soundcpu = machine->device("soundcpu");
	state->mcu = machine->device("mcu");
	state->ymsnd = machine->device("ymsnd");

	state_save_register_global(machine, state->disable_screen_blanking);
	state_save_register_global(machine, state->mj_input_num);
	state_save_register_global(machine, state->mj_last_val);
	state_save_register_global(machine, state->hwc_input_value);
	state_save_register_global(machine, state->atomicp_sound_divisor);
	state_save_register_global_pointer(machine, segaic16_spriteram_0, 0x02000/2);
	state_save_register_global_pointer(machine, segaic16_paletteram,  0x01000/2);
	state_save_register_global_pointer(machine, segaic16_tileram_0,   0x10000/2);
	state_save_register_global_pointer(machine, segaic16_textram_0,   0x01000/2);
	state_save_register_global_pointer(machine, workram,              0x40000/2);
}


static TIMER_CALLBACK( suspend_i8751 )
{
	segas1x_state *state = machine->driver_data<segas1x_state>();
	cpu_suspend(state->mcu, SUSPEND_REASON_DISABLE, 1);
}


/*************************************
 *
 *  Initialization & interrupts
 *
 *************************************/

static MACHINE_RESET( system16c )
{
	segas1x_state *state = machine->driver_data<segas1x_state>();
	static const UINT8 default_banklist[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
	int i;

	segaic16_memory_mapper_reset(machine);
	if (state->i8751_initial_config != NULL)
		segaic16_memory_mapper_config(machine, state->i8751_initial_config);
	segaic16_tilemap_reset(machine, 0);

	/* if we have a fake i8751 handler, disable the actual 8751 */
	if (state->i8751_vblank_hook != NULL)
		machine->scheduler().synchronize(FUNC(suspend_i8751));

	/* configure sprite banks */
	for (i = 0; i < 16; i++)
		segaic16_sprites_set_bank(machine, 0, i, default_banklist[i]);
}

/*************************************
 *
 *  I/O space
 *
 *************************************/

static READ16_HANDLER( standard_io_r )
{
	offset &= 0x1fff;
	switch (offset & (0x3000/2))
	{
		case 0x1000/2:
		{
			static const char *const sysports[] = { "SERVICE", "P1", "UNUSED", "P2" };
			return input_port_read(space->machine, sysports[offset & 3]);
		}

		case 0x2000/2:
			return input_port_read(space->machine, (offset & 1) ? "DSW2" : "DSW1");
	}
	logerror("%06X:standard_io_r - unknown read access to address %04X\n", cpu_get_pc(space->cpu), offset * 2);
	return segaic16_open_bus_r(space, 0, mem_mask);
}

static WRITE16_HANDLER( standard_io_w )
{
	segas1x_state *state = space->machine->driver_data<segas1x_state>();

	offset &= 0x1fff;
	switch (offset & (0x3000/2))
	{
		case 0x0000/2:
			/*
                D7 : 1 for most games, 0 for ddux, sdi, wb3
                D6 : 1= Screen flip, 0= Normal screen display
                D5 : 1= Display on, 0= Display off
                D4 : 0 for most games, 1 for eswat
                D3 : Output to lamp 2 (1= On, 0= Off)
                D2 : Output to lamp 1 (1= On, 0= Off)
                D1 : (Output to coin counter 2?)
                D0 : Output to coin counter 1
            */
			segaic16_tilemap_set_flip(space->machine, 0, data & 0x40);
			segaic16_sprites_set_flip(space->machine, 0, data & 0x40);
			if (!state->disable_screen_blanking)
				segaic16_set_display_enable(space->machine, data & 0x20);
			set_led_status(space->machine, 1, data & 0x08);
			set_led_status(space->machine, 0, data & 0x04);
			coin_counter_w(space->machine, 1, data & 0x02);
			coin_counter_w(space->machine, 0, data & 0x01);
			return;
	}
	logerror("%06X:standard_io_w - unknown write access to address %04X = %04X & %04X\n", cpu_get_pc(space->cpu), offset * 2, data, mem_mask);
}


static READ16_HANDLER( misc_io_r )
{
	segas1x_state *state = space->machine->driver_data<segas1x_state>();

	if (state->custom_io_r)
		return (*state->custom_io_r)(space, offset, mem_mask);
	else
		return standard_io_r(space, offset, mem_mask);
}


static WRITE16_HANDLER( misc_io_w )
{
	segas1x_state *state = space->machine->driver_data<segas1x_state>();

	if (state->custom_io_w)
		(*state->custom_io_w)(space, offset, data, mem_mask);
	else
		standard_io_w(space, offset, data, mem_mask);
}



/*************************************
 *
 *  Tile banking/math chips
 *
 *************************************/

static WRITE16_HANDLER( rom_5704_bank_w )
{
	if (ACCESSING_BITS_0_7)
		segaic16_tilemap_set_bank(space->machine, 0, offset & 1, data & 7);
}

/*************************************
 *
 *  Sound interaction
 *
 *************************************/

static WRITE8_DEVICE_HANDLER( upd7759_control_w )
{
	int size = device->machine->region("soundcpu")->bytes() - 0x10000;
	if (size > 0)
	{
		int bankoffs = 0;

		/* it is important to write in this order: if the /START line goes low
           at the same time /RESET goes low, no sample should be started */
		upd7759_start_w(device, data & 0x80);
		upd7759_reset_w(device, data & 0x40);

				/*
                    D5 : Unused
                    D4 : A17 for all ROMs
                    D3 : ROM select 0=A11, 1=A12
                    D2 : A16 for all ROMs
                    D1 : A15 for all ROMs
                    D0 : A14 for all ROMs
                */
		bankoffs = ((data & 0x08) >> 3) * 0x20000;
		bankoffs += (data & 0x07) * 0x04000;

		memory_set_bankptr(device->machine, "bank1", device->machine->region("soundcpu")->base() + 0x10000 + (bankoffs % size));
	}
}


static READ8_DEVICE_HANDLER( upd7759_status_r )
{
	return upd7759_busy_r(device) << 7;
}


static void upd7759_generate_nmi(device_t *device, int state)
{
	segas1x_state *driver = device->machine->driver_data<segas1x_state>();

	if (state)
		cpu_set_input_line(driver->soundcpu, INPUT_LINE_NMI, PULSE_LINE);
}


#if 0
static WRITE8_HANDLER( mcu_data_w )
{
	segas1x_state *state = space->machine->driver_data<segas1x_state>();
	state->mcu_data = data;
	generic_pulse_irq_line(state->mcu, 1);
}
#endif


/*************************************
 *
 *  I8751 interrupt generation
 *
 *************************************/

static INTERRUPT_GEN( i8751_main_cpu_vblank )
{
	segas1x_state *state = device->machine->driver_data<segas1x_state>();

	/* if we have a fake 8751 handler, call it on VBLANK */
	if (state->i8751_vblank_hook != NULL)
		(*state->i8751_vblank_hook)(device->machine);
}



/*************************************
 *
 *  Per-game I8751 workarounds
 *
 *************************************/

static void fz2dx_i8751_sim(running_machine *machine)
{
	segas1x_state *state = machine->driver_data<segas1x_state>();

	/* signal a VBLANK to the main CPU */
	cpu_set_input_line(state->maincpu, 4, HOLD_LINE);
}

/*************************************
 *
 *  Capacitor-backed RAM
 *
 *************************************/

static NVRAM_HANDLER( system16c )
{
	if (read_or_write)
		file->write(workram, 0x40000);
	else if (file)
		file->read(workram, 0x40000);
}

/*************************************
 *
 *  Main CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( system16c_map, ADDRESS_SPACE_PROGRAM, 16 )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x000000, 0xffffff) AM_READWRITE(segaic16_memory_mapper_lsb_r, segaic16_memory_mapper_lsb_w)
ADDRESS_MAP_END



/*************************************
 *
 *  Sound CPU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( sound_map, ADDRESS_SPACE_PROGRAM, 8 )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x7fff) AM_ROM
	AM_RANGE(0x8000, 0xdfff) AM_ROMBANK("bank1")
	AM_RANGE(0xe800, 0xe800) AM_READ(soundlatch_r)
	AM_RANGE(0xf800, 0xffff) AM_RAM
ADDRESS_MAP_END

static ADDRESS_MAP_START( sound_portmap, ADDRESS_SPACE_IO, 8 )
	ADDRESS_MAP_UNMAP_HIGH
	ADDRESS_MAP_GLOBAL_MASK(0xff)
	AM_RANGE(0x00, 0x01) AM_MIRROR(0x3e) AM_DEVREADWRITE("ymsnd", ym2151_r, ym2151_w)
	AM_RANGE(0x40, 0x40) AM_MIRROR(0x3f) AM_DEVWRITE("upd", upd7759_control_w)
	AM_RANGE(0x80, 0x80) AM_MIRROR(0x3f) AM_DEVREADWRITE("upd", upd7759_status_r, upd7759_port_w)
	AM_RANGE(0xc0, 0xc0) AM_MIRROR(0x3f) AM_READ(soundlatch_r)
ADDRESS_MAP_END



/*************************************
 *
 *  i8751 MCU memory handlers
 *
 *************************************/

static ADDRESS_MAP_START( mcu_io_map, ADDRESS_SPACE_IO, 8 )
	ADDRESS_MAP_UNMAP_HIGH
	AM_RANGE(0x0000, 0x001f) AM_MIRROR(0xff00) AM_READWRITE(segaic16_memory_mapper_r, segaic16_memory_mapper_w)
ADDRESS_MAP_END



/*************************************
 *
 *  Generic port definitions
 *
 *************************************/

static INPUT_PORTS_START( system16c_generic )
	PORT_START("SERVICE")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_COIN1 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_COIN2 )
	PORT_SERVICE_NO_TOGGLE( 0x04, IP_ACTIVE_LOW )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_SERVICE1 )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_START1 )
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_START2 )
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_UNKNOWN )

	PORT_START("P1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 )
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 )
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON2 )
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY

	PORT_START("UNUSED")
	PORT_BIT( 0xff, IP_ACTIVE_LOW, IPT_UNUSED )

	PORT_START("P2")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_COCKTAIL
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_COCKTAIL
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_COCKTAIL
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_UNKNOWN )
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_JOYSTICK_DOWN ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_JOYSTICK_UP ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_JOYSTICK_RIGHT ) PORT_8WAY PORT_COCKTAIL
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_JOYSTICK_LEFT ) PORT_8WAY PORT_COCKTAIL

	PORT_START("DSW1")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:2")
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:3")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:6")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:7")
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:8")
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )

	PORT_START("DSW2")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:2")
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x04, 0x04, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:3")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x08, 0x08, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:4")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:6")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x40, 0x40, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:7")
	PORT_DIPSETTING(    0x40, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x80, 0x80, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWA:8")
	PORT_DIPSETTING(    0x80, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
INPUT_PORTS_END



/*************************************
 *
 *  Game-specific port definitions
 *
 *************************************/

 static INPUT_PORTS_START( fz2dx )
	PORT_INCLUDE( system16c_generic )

	PORT_MODIFY("DSW1")
	PORT_DIPNAME( 0x01, 0x01, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:1")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x02, 0x02, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:2")
	PORT_DIPSETTING(    0x02, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x0c, 0x0c, DEF_STR( Lives ) ) PORT_DIPLOCATION("SWB:3,4")
	PORT_DIPSETTING(    0x08, "2" )
	PORT_DIPSETTING(    0x0c, "3" )
	PORT_DIPSETTING(    0x04, "4" )
	PORT_DIPSETTING(    0x00, "Unlimited" )
	PORT_DIPNAME( 0x10, 0x10, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:5")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0x20, 0x20, DEF_STR( Unknown ) ) PORT_DIPLOCATION("SWB:6")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ) )
	PORT_DIPSETTING(    0x00, DEF_STR( On ) )
	PORT_DIPNAME( 0xc0, 0xc0, DEF_STR( Difficulty ) ) PORT_DIPLOCATION("SWB:7,8")
	PORT_DIPSETTING(    0x80, DEF_STR( Easy ) )
	PORT_DIPSETTING(    0xc0, DEF_STR( Normal ) )
	PORT_DIPSETTING(    0x40, DEF_STR( Hard ) )
	PORT_DIPSETTING(    0x00, DEF_STR( Hardest ) )
INPUT_PORTS_END



/*************************************
 *
 *  Sound definitions
 *
 *************************************/

static const upd7759_interface upd7759_config =
{
	upd7759_generate_nmi
};



/*************************************
 *
 *  Graphics definitions
 *
 *************************************/

static GFXDECODE_START( segas16c )
	GFXDECODE_ENTRY( "gfx1", 0, gfx_8x8x3_planar,	0, 1024 )
GFXDECODE_END



/*************************************
 *
 *  Generic machine drivers
 *
 *************************************/

static MACHINE_CONFIG_START( system16c, segas1x_state )

	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", M68000, MASTER_CLOCK_10MHz)
	MCFG_CPU_PROGRAM_MAP(system16c_map)
	MCFG_CPU_VBLANK_INT("screen", irq4_line_hold)

	MCFG_CPU_ADD("soundcpu", Z80, MASTER_CLOCK_10MHz/2)
	MCFG_CPU_PROGRAM_MAP(sound_map)
	MCFG_CPU_IO_MAP(sound_portmap)

	MCFG_MACHINE_RESET(system16c)
	MCFG_NVRAM_HANDLER(system16c)

	/* video hardware */
	MCFG_GFXDECODE(segas16c)
	MCFG_PALETTE_LENGTH(2048*3)

	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_FORMAT(BITMAP_FORMAT_INDEXED16)
	MCFG_SCREEN_RAW_PARAMS(MASTER_CLOCK_25MHz/4, 400, 0, 320, 262, 0, 224)
	MCFG_SCREEN_UPDATE(system16c)

	MCFG_VIDEO_START(system16c)

	MCFG_SEGA16SP_ADD_16B("segaspr1")

	/* sound hardware */
	MCFG_SPEAKER_STANDARD_MONO("mono")

	MCFG_SOUND_ADD("ymsnd", YM2151, MASTER_CLOCK_8MHz/2)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)

	MCFG_SOUND_ADD("upd", UPD7759, UPD7759_STANDARD_CLOCK)
	MCFG_SOUND_CONFIG(upd7759_config)
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 1.00)
MACHINE_CONFIG_END


static MACHINE_CONFIG_DERIVED( system16c_8751, system16c )
	MCFG_CPU_MODIFY("maincpu")
	MCFG_CPU_VBLANK_INT("screen", i8751_main_cpu_vblank)

	MCFG_CPU_ADD("mcu", I8751, MASTER_CLOCK_8MHz)
	MCFG_CPU_IO_MAP(mcu_io_map)
	MCFG_CPU_VBLANK_INT("screen", irq0_line_pulse)
MACHINE_CONFIG_END



/*************************************
 *
 *  ROM definition(s)
 *
 *************************************/

/**************************************************************************************************************************
 **************************************************************************************************************************
 **************************************************************************************************************************
    Fantasy Zone II System16C
    CPU: 68000
    ROM Board: 171-5704 custom
*/
ROM_START( fz2dx )
	ROM_REGION( 0xc0000, "maincpu", 0 )					/* 68000 code */
	ROM_LOAD16_WORD_SWAP( "fz2_s16c.p00", 0x00000, 0x40000, CRC(B7D16C1D) SHA1(7587A0E4FA64664F53D7BA48D711B6D26ADD6220) )
	ROM_LOAD16_WORD_SWAP( "fz2_s16c.p01", 0x80000, 0x40000, CRC(2C47487C) SHA1(0E3A524DAE50E5B099396EF712CE45EA147B424B) )

	ROM_REGION( 0x60000, "gfx1", 0 )	/* tiles */
	ROM_LOAD( "fz2_s16c.bg",  0x00000, 0x60000, CRC(C092DC23) SHA1(CC7980B8AF9FED7A1CDF70CBBE6A2A67BB79594F) )

	ROM_REGION16_BE( 0x200000, "gfx2", 0 )				/* sprites */
	ROM_LOAD( "fz2_s16c.obj", 0x000000, 0x200000, CRC(57753F79) SHA1(7566CEF4344FBE7FB7ADC476113CD6E8780AEEF4) )

	ROM_REGION( 0x50000, "soundcpu", 0 )					/* sound CPU */
	ROM_LOAD( "fz2_s16c.snd", 0x00000, 0x30000, CRC(0ED30EC1) SHA1(EDF2DBD6A35394849E0419C518C6FB0F4ACCB9D1) )

	ROM_REGION( 0x1000, "mcu", 0 )	/* protection MCU */
	ROM_LOAD( "317-0000.bin", 0x00000, 0x1000, NO_DUMP )
ROM_END


/*************************************
 *
 *  Game-specific driver inits
 *
 *************************************/

static DRIVER_INIT( fz2dx_8751 )
{
	segas1x_state *state = machine->driver_data<segas1x_state>();
//	DRIVER_INIT_CALL(fz2dx_8751);
	system16c_generic_init(machine);
	state->i8751_vblank_hook = fz2dx_i8751_sim;
}

/*************************************
 *
 *  Game driver(s)
 *
 *************************************/
GAME( 1987, fz2dx, 0, system16c_8751, fz2dx, fz2dx_8751, ROT0, "Sega / M2", "Fantasy Zone II DX", 0 )
