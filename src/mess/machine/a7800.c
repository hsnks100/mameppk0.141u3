/***************************************************************************

    a7800.c

    Machine file to handle emulation of the Atari 7800.

     5-Nov-2003 npwoods     Cleanups

    14-May-2002 kubecj      Fixed Fatal Run - adding simple riot timer helped.
                            maybe someone with knowledge should add full fledged
                            riot emulation?

    13-May-2002 kubecj      Fixed a7800_cart_type not to be too short ;-D
                            fixed for loading of bank6 cart (uh, I hope)
                            fixed for loading 64k supercarts
                            fixed for using PAL bios
                            cart not needed when in PAL mode
                            added F18 Hornet bank select type
                            added Activision bank select type

***************************************************************************/

#include "emu.h"
#include "includes/a7800.h"
#include "cpu/m6502/m6502.h"
#include "sound/tiasound.h"
#include "machine/6532riot.h"
#include "sound/pokey.h"
#include "sound/tiaintf.h"
#include "hash.h"



/* local */



/***************************************************************************
    6532 RIOT
***************************************************************************/

static READ8_DEVICE_HANDLER( riot_joystick_r )
{
	return input_port_read(device->machine, "joysticks");
}

static READ8_DEVICE_HANDLER( riot_console_button_r )
{
	return input_port_read(device->machine, "console_buttons");
}

const riot6532_interface a7800_r6532_interface =
{
	DEVCB_HANDLER(riot_joystick_r),
	DEVCB_HANDLER(riot_console_button_r),
	DEVCB_NULL,
	DEVCB_NULL
};


/***************************************************************************
    DRIVER INIT
***************************************************************************/

static void a7800_driver_init(running_machine *machine, int ispal, int lines)
{
	a7800_state *state = machine->driver_data<a7800_state>();
	address_space* space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	state->ROM = machine->region("maincpu")->base();
	state->ispal = ispal;
	state->lines = lines;

	/* standard banks */
	memory_set_bankptr(machine, "bank5", &state->ROM[0x2040]);		/* RAM0 */
	memory_set_bankptr(machine, "bank6", &state->ROM[0x2140]);		/* RAM1 */
	memory_set_bankptr(machine, "bank7", &state->ROM[0x2000]);		/* MAINRAM */

	/* Brutal hack put in as a consequence of new memory system; fix this */
	memory_install_readwrite_bank(space, 0x0480, 0x04FF, 0, 0,"bank10");
	memory_set_bankptr(machine, "bank10", state->ROM + 0x0480);
	memory_install_readwrite_bank(space, 0x1800, 0x27FF, 0, 0, "bank11");
	memory_set_bankptr(machine, "bank11", state->ROM + 0x1800);
}


DRIVER_INIT( a7800_ntsc )
{
	a7800_driver_init(machine, FALSE, 262);
}


DRIVER_INIT( a7800_pal )
{
	a7800_driver_init(machine, TRUE, 312);
}


MACHINE_RESET( a7800 )
{
	a7800_state *state = machine->driver_data<a7800_state>();
	UINT8 *memory;
	address_space* space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);

	state->ctrl_lock = 0;
	state->ctrl_reg = 0;
	state->maria_flag = 0;

	/* set banks to default states */
	memory = machine->region("maincpu")->base();
	memory_set_bankptr(machine,  "bank1", memory + 0x4000 );
	memory_set_bankptr(machine,  "bank2", memory + 0x8000 );
	memory_set_bankptr(machine,  "bank3", memory + 0xA000 );
	memory_set_bankptr(machine,  "bank4", memory + 0xC000 );

	/* pokey cartridge */
	if (state->cart_type & 0x01)
	{
		device_t *pokey = machine->device("pokey");
		memory_install_read8_device_handler(space, pokey, 0x4000, 0x7FFF, 0, 0, pokey_r);
		memory_install_write8_device_handler(space, pokey, 0x4000, 0x7FFF, 0, 0, pokey_w);
	}
}


/***************************************************************************
    CARTRIDGE HANDLING
***************************************************************************/

#define MBANK_TYPE_ATARI 0x0000
#define MBANK_TYPE_ACTIVISION 0x0100
#define MBANK_TYPE_ABSOLUTE 0x0200

/*  Header format
0     Header version     - 1 byte
1..16  "ATARI7800     "  - 16 bytes
17..48 Cart title        - 32 bytes
49..52 data length      - 4 bytes
53..54 cart type          - 2 bytes
    bit 0 0x01 - pokey cart
    bit 1 0x02 - supercart bank switched
    bit 2 0x04 - supercart RAM at $4000
    bit 3 0x08 - additional state->ROM at $4000

    bit 8-15 - Special
        0 = Normal cart
        1 = Absolute (F18 Hornet)
        2 = Activision

55   controller 1 type  - 1 byte
56   controller 2 type  - 1 byte
    0 = None
    1 = Joystick
    2 = Light Gun
57  0 = NTSC/1 = PAL

100..127 "ACTUAL CART DATA STARTS HERE" - 28 bytes

Versions:
    Version 0: Initial release
    Version 1: Added PAL/NTSC bit. Added Special cart byte.
               Changed 53 bit 2, added bit 3

*/
void a7800_partialhash(hash_collection &dest, const unsigned char *data,
	unsigned long length, const char *functions)
{
	if (length <= 128)
		return;
	dest.compute(&data[128], length - 128, functions);	
}


static int a7800_verify_cart(char header[128])
{
	const char* tag = "ATARI7800";

	if( strncmp( tag, header + 1, 9 ) )
	{
		logerror("Not a valid A7800 image\n");
		return IMAGE_VERIFY_FAIL;
	}

	logerror("returning ID_OK\n");
	return IMAGE_VERIFY_PASS;
}


DEVICE_START( a7800_cart )
{
	a7800_state *state = device->machine->driver_data<a7800_state>();
	UINT8 *memory = device->machine->region("maincpu")->base();

	state->bios_bkup = NULL;
	state->cart_bkup = NULL;

	/* Allocate memory for BIOS bank switching */
	state->bios_bkup = auto_alloc_array_clear(device->machine, UINT8, 0x4000);
	state->cart_bkup = auto_alloc_array(device->machine, UINT8, 0x4000);

	/* save the BIOS so we can switch it in and out */
	memcpy( state->bios_bkup, memory + 0xC000, 0x4000 );

	/* Initialize cart area to "no data" */
	memset( state->cart_bkup, 0xFF, 0x4000 );

	/* defaults for PAL bios without cart */
	state->cart_type = 0;
	state->stick_type = 1;
}

struct a7800_pcb
{
	const char *pcb_name;
	UINT16     type;
};

// sketchy support for a7800 cart types
// TODO: proper emulation of the banking based on xml
// (and on the real cart layout!)
static const a7800_pcb pcb_list[] =
{
	{ "ABSOLUTE", MBANK_TYPE_ABSOLUTE },
	{ "ACTIVISION", MBANK_TYPE_ACTIVISION },
	{ "TYPE-0", 0x0 },
	{ "TYPE-1", 0x1 },
	{ "TYPE-2", 0x2 },
	{ "TYPE-3", 0x3 },
	{ "TYPE-6", 0x6 },
	{ "TYPE-A", 0xa },
	{ 0 }
};

static UINT16 a7800_get_pcb_id(const char *pcb)
{
	int	i;

	for (i = 0; i < ARRAY_LENGTH(pcb_list); i++)
	{
		if (!mame_stricmp(pcb_list[i].pcb_name, pcb))
			return pcb_list[i].type;
	}

	return 0;
}

DEVICE_IMAGE_LOAD( a7800_cart )
{
	a7800_state *state = image.device().machine->driver_data<a7800_state>();
	UINT32 len = 0, start = 0;
	unsigned char header[128];
	UINT8 *memory = image.device().machine->region("maincpu")->base();
	const char	*pcb_name;

	// detect cart type either from xml or from header
	if (image.software_entry() == NULL)
	{
		/* Load and decode the header */
		image.fread(header, 128);

		/* Check the cart */
		if( a7800_verify_cart((char *)header) == IMAGE_VERIFY_FAIL)
			return IMAGE_INIT_FAIL;

		len =(header[49] << 24) |(header[50] << 16) |(header[51] << 8) | header[52];
		state->cart_size = len;

		state->cart_type =(header[53] << 8) | header[54];
		state->stick_type = header[55];
		logerror("Cart type: %x\n", state->cart_type);

		/* For now, if game support stick and gun, set it to stick */
		if (state->stick_type == 3)
			state->stick_type = 1;
	}
	else
	{
		len = image.get_software_region_length("rom");
		state->cart_size = len;
		// TODO: add stick/gun support to xml!
		state->stick_type = 1;
		if ((pcb_name = image.get_feature("pcb_type")) == NULL)
			state->cart_type = 0;
		else
			state->cart_type = a7800_get_pcb_id(pcb_name);
	}

	if (state->cart_type == 0 || state->cart_type == 1)
	{
		/* Normal Cart */
		start = 0x10000 - len;
		state->cartridge_rom = memory + start;
		if (image.software_entry() == NULL)
			image.fread(state->cartridge_rom, len);
		else
			memcpy(state->cartridge_rom, image.get_software_region("rom"), len);
	}
	else if (state->cart_type & 0x02)
	{
		/* Super Cart */
		/* Extra ROM at $4000 */
		if (state->cart_type & 0x08)
		{
			if (image.software_entry() == NULL)
				image.fread(memory + 0x4000, 0x4000);
			else
				memcpy(memory + 0x4000, image.get_software_region("rom"), 0x4000);
			len -= 0x4000;
			start = 0x4000;
		}

		state->cartridge_rom = memory + 0x10000;
		if (image.software_entry() == NULL)
			image.fread(state->cartridge_rom, len);
		else
			memcpy(state->cartridge_rom, image.get_software_region("rom") + start, len);

		/* bank 0 */
		memcpy(memory + 0x8000, memory + 0x10000, 0x4000);

		/* last bank */
		memcpy(memory + 0xC000, memory + 0x10000 + len - 0x4000, 0x4000);

		/* fixed 2002/05/13 kubecj
            there was 0x08, I added also two other cases.
            Now, it loads bank n-2 to $4000 if it's empty.
        */

		/* bank n-2 */
		if (!(state->cart_type & 0x0d))
		{
			memcpy(memory + 0x4000, memory + 0x10000 + len - 0x8000, 0x4000);
		}
	}
	else if (state->cart_type == MBANK_TYPE_ABSOLUTE)
	{
		/* F18 Hornet */

		logerror("Cart type: %x Absolute\n",state->cart_type);

		state->cartridge_rom = memory + 0x10000;
		if (image.software_entry() == NULL)
			image.fread(state->cartridge_rom, len);
		else
			memcpy(state->cartridge_rom, image.get_software_region("rom") + start, len);

		/* bank 0 */
		memcpy(memory + 0x4000, memory + 0x10000, 0x4000);

		/* last bank */
		memcpy(memory + 0x8000, memory + 0x18000, 0x8000);
	}
	else if (state->cart_type == MBANK_TYPE_ACTIVISION)
	{
		/* Activision */

		logerror("Cart type: %x Activision\n",state->cart_type);

		state->cartridge_rom = memory + 0x10000;
		if (image.software_entry() == NULL)
			image.fread(state->cartridge_rom, len);
		else
			memcpy(state->cartridge_rom, image.get_software_region("rom") + start, len);

		/* bank 0 */
		memcpy(memory + 0xa000, memory + 0x10000, 0x4000);

		/* bank6 hi */
		memcpy(memory + 0x4000, memory + 0x2a000, 0x2000);

		/* bank6 lo */
		memcpy(memory + 0x6000, memory + 0x28000, 0x2000);

		/* bank7 hi */
		memcpy(memory + 0x8000, memory + 0x2e000, 0x2000);

		/* bank7 lo */
		memcpy(memory + 0xe000, memory + 0x2c000, 0x2000);

	}

	memcpy(state->cart_bkup, memory + 0xc000, 0x4000);
	memcpy(memory + 0xc000, state->bios_bkup, 0x4000);
	return IMAGE_INIT_PASS;
}


WRITE8_HANDLER( a7800_RAM0_w )
{
	a7800_state *state = space->machine->driver_data<a7800_state>();
	state->ROM[0x2040 + offset] = data;
	state->ROM[0x40 + offset] = data;
}


WRITE8_HANDLER( a7800_cart_w )
{
	a7800_state *state = space->machine->driver_data<a7800_state>();
	UINT8 *memory = space->machine->region("maincpu")->base();

	if(offset < 0x4000)
	{
		if(state->cart_type & 0x04)
		{
			state->ROM[0x4000 + offset] = data;
		}
		else if(state->cart_type & 0x01)
		{
			device_t *pokey = space->machine->device("pokey");
			pokey_w(pokey, offset, data);
		}
		else
		{
			logerror("Undefined write A: %x",offset + 0x4000);
		}
	}

	if(( state->cart_type & 0x02 ) &&( offset >= 0x4000 ) )
	{
		/* fix for 64kb supercart */
		if( state->cart_size == 0x10000 )
		{
			data &= 0x03;
		}
		else
		{
			data &= 0x07;
		}
		memory_set_bankptr(space->machine, "bank2",memory + 0x10000 + (data << 14));
		memory_set_bankptr(space->machine, "bank3",memory + 0x12000 + (data << 14));
	/*  logerror("BANK SEL: %d\n",data); */
	}
	else if(( state->cart_type == MBANK_TYPE_ABSOLUTE ) &&( offset == 0x4000 ) )
	{
		/* F18 Hornet */
		/*logerror( "F18 BANK SEL: %d\n", data );*/
		if( data & 1 )
		{
			memory_set_bankptr(space->machine, "bank1",memory + 0x10000 );
		}
		else if( data & 2 )
		{
			memory_set_bankptr(space->machine, "bank1",memory + 0x14000 );
		}
	}
	else if(( state->cart_type == MBANK_TYPE_ACTIVISION ) &&( offset >= 0xBF80 ) )
	{
		/* Activision */
		data = offset & 7;

		/*logerror( "Activision BANK SEL: %d\n", data );*/

		memory_set_bankptr(space->machine,  "bank3", memory + 0x10000 + ( data << 14 ) );
		memory_set_bankptr(space->machine,  "bank4", memory + 0x12000 + ( data << 14 ) );
	}
}


/***************************************************************************
    TIA
***************************************************************************/

READ8_HANDLER( a7800_TIA_r )
{
	switch(offset)
	{
		case 0x08:
			  return((input_port_read(space->machine, "buttons") & 0x02) << 6);
		case 0x09:
			  return((input_port_read(space->machine, "buttons") & 0x08) << 4);
		case 0x0A:
			  return((input_port_read(space->machine, "buttons") & 0x01) << 7);
		case 0x0B:
			  return((input_port_read(space->machine, "buttons") & 0x04) << 5);
		case 0x0c:
			if((input_port_read(space->machine, "buttons") & 0x08) ||(input_port_read(space->machine, "buttons") & 0x02))
				return 0x00;
			else
				return 0x80;
		case 0x0d:
			if((input_port_read(space->machine, "buttons") & 0x01) ||(input_port_read(space->machine, "buttons") & 0x04))
				return 0x00;
			else
				return 0x80;
		default:
			logerror("undefined TIA read %x\n",offset);

	}
	return 0xFF;
}


WRITE8_HANDLER( a7800_TIA_w )
{
	a7800_state *state = space->machine->driver_data<a7800_state>();
	switch(offset) {
	case 0x01:
		if(data & 0x01)
		{
			state->maria_flag=1;
		}
		if(!state->ctrl_lock)
		{
			state->ctrl_lock = data & 0x01;
			state->ctrl_reg = data;

			if (data & 0x04)
				memcpy( state->ROM + 0xC000, state->cart_bkup, 0x4000 );
			else
				memcpy( state->ROM + 0xC000, state->bios_bkup, 0x4000 );
		}
		break;
	}
	tia_sound_w(space->machine->device("tia"), offset, data);
	state->ROM[offset] = data;
}
