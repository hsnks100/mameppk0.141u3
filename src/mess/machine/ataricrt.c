/***************************************************************************

    MESS specific Atari init and Cartridge code for Atari 8 bit systems

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "includes/atari.h"
#include "ataridev.h"
#include "machine/ram.h"
#include "hashfile.h"

#define LEFT_CARTSLOT_MOUNTED  1
#define RIGHT_CARTSLOT_MOUNTED 2

/* PCB */
enum
{
    A800_UNKNOWN = 0,
	A800_4K, A800_8K, A800_12K, A800_16K,
	A800_RIGHT_4K, A800_RIGHT_8K,
	OSS_034M, OSS_M091, PHOENIX_8K, XEGS_32K,
	BBSB, DIAMOND_64K, WILLIAMS_64K, EXPRESS_64,
	SPARTADOS_X
};

static int a800_cart_loaded = 0;
static int atari = 0;
static int a800_cart_type = A800_UNKNOWN;

/*************************************
 *
 *  Generic code
 *
 *************************************/


// Currently, the drivers have fixed 40k RAM, however the function here is ready for different sizes too
static void a800_setbank(running_machine *machine, int cart_mounted)
{
	offs_t ram_top;

	// take care of 0x0000-0x7fff: RAM or NOP
	ram_top = MIN(ram_get_size(machine->device(RAM_TAG)), 0x8000) - 1;
	memory_install_readwrite_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x0000, ram_top, 0, 0, "0000");
	memory_set_bankptr(machine, "0000", ram_get_ptr(machine->device(RAM_TAG)));

	// take care of 0x8000-0x9fff: A800 -> either right slot or RAM or NOP, others -> RAM or NOP
	// is there anything in the right slot?
	if (cart_mounted & RIGHT_CARTSLOT_MOUNTED)
	{
		memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x9fff, 0, 0, "8000");
		memory_set_bankptr(machine, "8000", machine->region("rslot")->base());
		memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x9fff, 0, 0);
	}
	else if (a800_cart_type != BBSB)
	{
		ram_top = MIN(ram_get_size(machine->device(RAM_TAG)), 0xa000) - 1;
		if (ram_top > 0x8000)
		{
			memory_install_readwrite_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, ram_top, 0, 0, "8000");
			memory_set_bankptr(machine, "8000", ram_get_ptr(machine->device(RAM_TAG)) + 0x8000);
		}
	}

	// take care of 0xa000-0xbfff: is there anything in the left slot?
	if (cart_mounted & LEFT_CARTSLOT_MOUNTED)
	{
		// FIXME: this is an hack to keep XL working until we clean up its memory map as well!
		if (atari == ATARI_800XL)
		{
			if (a800_cart_type == A800_16K)
			{
				memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x9fff, 0, 0, "8000");
				memory_set_bankptr(machine, "8000", machine->region("lslot")->base());
				memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x9fff, 0, 0);

				memcpy(machine->region("maincpu")->base() + 0x10000, machine->region("lslot")->base() + 0x2000, 0x2000);
			}
			else if (a800_cart_type == A800_8K)
				memcpy(machine->region("maincpu")->base() + 0x10000, machine->region("lslot")->base(), 0x2000);
			else
				fatalerror("This type of cart is not supported yet in this driver. Please use a400 or a800.\n");
		}
		else if (a800_cart_type == A800_16K)
		{
			memory_set_bankptr(machine, "8000", machine->region("lslot")->base());
			memory_set_bankptr(machine, "a000", machine->region("lslot")->base() + 0x2000);
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0xbfff, 0, 0);
		}
		else if (a800_cart_type == BBSB)
		{
			// this requires separate banking in 0x8000 & 0x9000!
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x8fff, 0, 0, "8000");
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x9000, 0x9fff, 0, 0, "9000");
			memory_set_bankptr(machine, "8000", machine->region("lslot")->base() + 0x0000);
			memory_set_bankptr(machine, "9000", machine->region("lslot")->base() + 0x4000);
			memory_set_bankptr(machine, "a000", machine->region("lslot")->base() + 0x8000);
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xbfff, 0, 0);
		}
		else if (a800_cart_type == OSS_034M)
		{
			// this requires separate banking in 0xa000 & 0xb000!
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xafff, 0, 0, "a000");
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xb000, 0xbfff, 0, 0, "b000");
			memory_set_bankptr(machine, "b000", machine->region("lslot")->base() + 0x3000);
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xbfff, 0, 0);
		}
		else if (a800_cart_type == OSS_M091)
		{
			// this requires separate banking in 0xa000 & 0xb000!
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xafff, 0, 0, "a000");
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xb000, 0xbfff, 0, 0, "b000");
			memory_set_bankptr(machine, "b000", machine->region("lslot")->base());
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xbfff, 0, 0);
		}
		else if (a800_cart_type == XEGS_32K)
		{
			memory_set_bankptr(machine, "8000", machine->region("lslot")->base());
			memory_set_bankptr(machine, "a000", machine->region("lslot")->base() + 0x6000);
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0xbfff, 0, 0);
		}
		else
		{
			memory_set_bankptr(machine, "a000", machine->region("lslot")->base());
			memory_unmap_write(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xa000, 0xbfff, 0, 0);
		}
	}
}

/* MESS specific parts that have to be started */
static void ms_atari_machine_start(running_machine *machine, int type, int has_cart)
{
	/* set atari type (temporarily not used) */
	atari = type;
	a800_setbank(machine, a800_cart_loaded);
}

static void ms_atari800xl_machine_start(running_machine *machine, int type, int has_cart)
{
	/* set atari type (temporarily not used) */
	atari = type;
	a800_setbank(machine, a800_cart_loaded);
}

/*************************************
 *
 *  Atari 400
 *
 *************************************/

MACHINE_START( a400 )
{
	atari_machine_start(machine);
	ms_atari_machine_start(machine, ATARI_400, TRUE);
}


/*************************************
 *
 *  Atari 800
 *
 *************************************/

MACHINE_START( a800 )
{
	atari_machine_start(machine);
	ms_atari_machine_start(machine, ATARI_800, TRUE);
}

static WRITE8_HANDLER( x32_bank_w )
{
	//  printf("written %x\n", data);
	int bank = data & 0x03;
	memory_set_bankptr(space->machine, "8000", space->machine->region("lslot")->base() + bank * 0x2000);
}

static WRITE8_HANDLER( w64_bank_w )
{
//  printf("write to %x\n", offset);

	if (offset < 8)
		memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + offset * 0x2000);
	else
		memory_set_bankptr(space->machine, "a000", space->machine->region("maincpu")->base());
	// FIXME: writes to 0x8-0xf should disable the cart
}

// this covers Express 64, Diamond 64 and SpartaDOS (same bankswitch, but at different addresses)
static WRITE8_HANDLER( ex64_bank_w )
{
//  printf("write to %x\n", offset);

	if (offset < 8)
		memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + (7 - offset) * 0x2000);
	else
		memory_set_bankptr(space->machine, "a000", space->machine->region("maincpu")->base());
	// FIXME: writes to 0x8-0xf should disable the cart
}

static WRITE8_HANDLER( bbsb_bankl_w )
{
//  printf("write to %x\n", 0x8000 + offset);
	if (offset >= 0xff6 && offset <= 0xff9)
		memory_set_bankptr(space->machine, "8000", space->machine->region("lslot")->base() + 0x0000 + (offset - 0xff6) * 0x1000);
}

static WRITE8_HANDLER( bbsb_bankh_w )
{
//  printf("write to %x\n", 0x9000 + offset);
	if (offset >= 0xff6 && offset <= 0xff9)
		memory_set_bankptr(space->machine, "9000", space->machine->region("lslot")->base() + 0x4000 + (offset - 0xff6) * 0x1000);
}

static WRITE8_HANDLER( oss_034m_w )
{
	switch (offset & 0x0f)
	{
		case 0:
		case 1:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base());
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base() + 0x3000);
			break;
		case 2:
		case 6:
			// docs says this should put 0xff in the 0xa000 bank -> let's point to the end of the cart
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x4000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base() + 0x3000);
			break;
		case 3:
		case 7:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x1000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base() + 0x3000);
			break;
		case 4:
		case 5:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x2000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base() + 0x3000);
			break;
		default:
			memory_set_bankptr(space->machine, "a000", space->machine->region("maincpu")->base() + 0xa000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("maincpu")->base() + 0xb000);
			break;
	}
}

static WRITE8_HANDLER( oss_m091_w )
{
	switch (offset & 0x09)
	{
		case 0:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x1000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base());
			break;
		case 1:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x3000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base());
			break;
		case 8:
			memory_set_bankptr(space->machine, "a000", space->machine->region("maincpu")->base() + 0xa000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("maincpu")->base() + 0xb000);
			break;
		case 9:
			memory_set_bankptr(space->machine, "a000", space->machine->region("lslot")->base() + 0x2000);
			memory_set_bankptr(space->machine, "b000", space->machine->region("lslot")->base());
			break;
	}
}

#if 0
static int bbsb_bankl = 0;
static int bbsb_bankh = 0;

static WRITE8_HANDLER( bbsb_bankl_w )
{
	bbsb_bankl = offset; // 0,1,2,3
}

static WRITE8_HANDLER( bbsb_bankh_w )
{
	bbsb_bankh = offset; // 4,5,6,7
}

static READ8_HANDLER( bbsb_bankl_r )
{
	// return data from the selected bank (0,1,2,3)
	UINT8 *mem = space->machine->region("lslot")->base();
	return &mem[0x0000 + bbsb_bankl * 0x1000];
}

static READ8_HANDLER( bbsb_bankh_r )
{
	// return data from the selected bank (4,5,6,7)
	UINT8 *mem = space->machine->region("lslot")->base();
	return &mem[0x4000 + bbsb_bankh * 0x1000];
}
#endif

typedef struct _a800_pcb  a800_pcb;
struct _a800_pcb
{
	const char              *pcb_name;
	int                     pcb_id;
};

// Here, we take the feature attribute from .xml (i.e. the PCB name) and we assign a unique ID to it
// WARNING: most of these are still unsupported by the driver
static const a800_pcb pcb_list[] =
{
	{"standard 4k", A800_8K},
	{"standard 8k", A800_8K},
	{"standard 12k", A800_16K},
	{"standard 16k", A800_16K},
	{"right slot 4k", A800_RIGHT_4K},
	{"right slot 8k", A800_RIGHT_8K},

	{"oss 034m", OSS_034M},
	{"oss m091", OSS_M091},
	{"phoenix 8k", PHOENIX_8K},
	{"xegs 32k", XEGS_32K},
	{"bbsb", BBSB},
	{"diamond 64k", DIAMOND_64K},
	{"williams 64k", WILLIAMS_64K},
	{"express 64", EXPRESS_64},
	{"spartados x", SPARTADOS_X},
	{"N/A", A800_UNKNOWN}
};

static int a800_get_pcb_id(const char *pcb)
{
	int	i;

	for (i = 0; i < ARRAY_LENGTH(pcb_list); i++)
	{
		if (!mame_stricmp(pcb_list[i].pcb_name, pcb))
			return pcb_list[i].pcb_id;
	}

	return A800_UNKNOWN;
}

// currently this does nothing, but it will eventually install the memory handlers required by the mappers
static void a800_setup_mappers(running_machine *machine, int type)
{
	switch (type)
	{
		case A800_4K:
		case A800_RIGHT_4K:
		case A800_12K:
		case A800_8K:
		case A800_16K:
		case A800_RIGHT_8K:
		case PHOENIX_8K:	// as normal 8k cart, but it can be disabled by writing to 0xd500-0xdfff
			break;
		case XEGS_32K:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd500, 0xd5ff, 0, 0, x32_bank_w);
			break;
		case OSS_034M:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd500, 0xd5ff, 0, 0, oss_034m_w);
			break;
		case OSS_M091:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd500, 0xd5ff, 0, 0, oss_m091_w);
			break;
		case BBSB:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x8000, 0x8fff, 0, 0, bbsb_bankl_w);
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x9000, 0x9fff, 0, 0, bbsb_bankh_w);
			break;
		case WILLIAMS_64K:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd500, 0xd50f, 0, 0, w64_bank_w);
			break;
		case DIAMOND_64K:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd5d0, 0xd5df, 0, 0, ex64_bank_w);
			break;
		case EXPRESS_64:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd570, 0xd57f, 0, 0, ex64_bank_w);
			break;
		case SPARTADOS_X:
			memory_install_write8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0xd5e0, 0xd5ef, 0, 0, ex64_bank_w);
			break;
		default:
			break;
	}
}

static int a800_get_type(device_image_interface &image)
{
	UINT8 header[16];
	image.fread(header, 0x10);
	int hdr_type, cart_type = A800_UNKNOWN;

	// add check of CART format
	if (strncmp((const char *)header, "CART", 4))
		fatalerror("Invalid header detected!\n");

	hdr_type = (header[4] << 24) + (header[5] << 16) +  (header[6] << 8) + (header[7] << 0);
	switch (hdr_type)
	{
		case 1:
			cart_type = A800_8K;
			break;
		case 2:
			cart_type = A800_16K;
			break;
		case 3:
			cart_type = OSS_034M;
			break;
		case 8:
			cart_type = WILLIAMS_64K;
			break;
		case 9:
			cart_type = DIAMOND_64K;
			break;
		case 10:
			cart_type = EXPRESS_64;
			break;
		case 11:
			cart_type = SPARTADOS_X;
			break;
		case 12:
			cart_type = XEGS_32K;
			break;
		case 15:
			cart_type = OSS_M091;
			break;
		case 18:
			cart_type = BBSB;
			break;
		case 21:
			cart_type = A800_RIGHT_8K;
			break;
		case 39:
			cart_type = PHOENIX_8K;
			break;
		case 4:
		case 6:
		case 7:
		case 16:
		case 19:
		case 20:
			fatalerror("Cart type \"%d\" means this is an Atari 5200 cart.\n", hdr_type);
			break;
		default:
			mame_printf_info("Cart type \"%d\" is currently unsupported.\n", hdr_type);
			break;
	}
	return cart_type;
}

static int a800_check_cart_type(device_image_interface &image)
{
	const char	*pcb_name;
	int type = A800_UNKNOWN;

	if (image.software_entry() == NULL)
	{
		UINT32 size = image.length();

		// check if there is an header, if so extract cart_type from it, otherwise
		// try to guess the cart_type from the file size (notice that after the
		// a800_get_type call, we point at the start of the data)
		if ((size % 0x1000) == 0x10)
			type = a800_get_type(image);
		else if (size == 0x4000)
			type = A800_16K;
		else if (size == 0x2000)
		{
			if (strcmp(image.device().tag(),"cart2") == 0)
				type = A800_RIGHT_8K;
			else
				type = A800_8K;
		}
	}
	else
	{
		if ((pcb_name = image.get_feature("cart_type")) != NULL)
			type = a800_get_pcb_id(pcb_name);

		switch (type)
		{
			case A800_UNKNOWN:
			case A800_4K:
			case A800_RIGHT_4K:
			case A800_12K:
			case A800_8K:
			case A800_16K:
			case A800_RIGHT_8K:
				break;
			default:
				mame_printf_info("Cart type \"%s\" currently unsupported.\n", pcb_name);
				break;
		}
	}

	if ((strcmp(image.device().tag(),"cart2") == 0) && (type != A800_RIGHT_8K))
		fatalerror("You cannot load this image '%s' in the right slot", image.filename());

	return type;
}

DEVICE_IMAGE_LOAD( a800_cart )
{
	UINT32 size, start = 0;

	a800_cart_loaded = a800_cart_loaded & ~LEFT_CARTSLOT_MOUNTED;
	a800_cart_type = a800_check_cart_type(image);

	a800_setup_mappers(image.device().machine, a800_cart_type);

	if (image.software_entry() == NULL)
	{
		size = image.length();
		// if there is an header, skip it
		if ((size % 0x1000) == 0x10)
		{
			size -= 0x10;
			start = 0x10;
		}
		image.fread(image.device().machine->region("lslot")->base(), size - start);
	}
	else
	{
		size = image.get_software_region_length("rom");
		memcpy(image.device().machine->region("lslot")->base(), image.get_software_region("rom"), size);
	}

	a800_cart_loaded |= (size > 0x0000) ? 1 : 0;

	logerror("%s loaded left cartridge '%s' size %dK\n", image.device().machine->gamedrv->name, image.filename(), size/1024);
	return IMAGE_INIT_PASS;
}

DEVICE_IMAGE_LOAD( a800_cart_right )
{
	UINT32 size, start = 0;

	a800_cart_loaded = a800_cart_loaded & ~RIGHT_CARTSLOT_MOUNTED;
	a800_cart_type = a800_check_cart_type(image);

	a800_setup_mappers(image.device().machine, a800_cart_type);

	if (image.software_entry() == NULL)
	{
		size = image.length();
		// if there is an header, skip it
		if ((size % 0x1000) == 0x10)
		{
			size -= 0x10;
			start = 0x10;
		}
		image.fread(image.device().machine->region("rslot")->base(), size - start);
	}
	else
	{
		size = image.get_software_region_length("rom");
		memcpy(image.device().machine->region("rslot")->base(), image.get_software_region("rom"), size);
	}

	a800_cart_loaded |= (size > 0x0000) ? 2 : 0;

	logerror("%s loaded right cartridge '%s' size 8K\n", image.device().machine->gamedrv->name, image.filename());
	return IMAGE_INIT_PASS;
}

DEVICE_IMAGE_UNLOAD( a800_cart )
{
	a800_cart_loaded = a800_cart_loaded & ~LEFT_CARTSLOT_MOUNTED;
	a800_cart_type = A800_UNKNOWN;
	a800_setbank(image.device().machine, a800_cart_loaded);
}

DEVICE_IMAGE_UNLOAD( a800_cart_right )
{
	a800_cart_loaded = a800_cart_loaded & ~RIGHT_CARTSLOT_MOUNTED;
	a800_cart_type = A800_UNKNOWN;
	a800_setbank(image.device().machine, a800_cart_loaded);
}


/*************************************
 *
 *  Atari 800XL
 *
 *************************************/

MACHINE_START( a800xl )
{
	atari_machine_start(machine);
	ms_atari800xl_machine_start(machine, ATARI_800XL, TRUE);
}

/*************************************
 *
 *  Atari 5200 console
 *
 *************************************/

MACHINE_START( a5200 )
{
	atari_machine_start(machine);
	ms_atari_machine_start(machine, ATARI_800XL, TRUE);
}


DEVICE_IMAGE_LOAD( a5200_cart )
{
	UINT8 *mem = image.device().machine->region("maincpu")->base();
	int size;
	if (image.software_entry() == NULL)
	{
		/* load an optional (dual) cartidge */
		size = image.fread(&mem[0x4000], 0x8000);
	} else {
		size = image.get_software_region_length("rom");
		memcpy(mem + 0x4000, image.get_software_region("rom"), size);
	}
	if (size<0x8000) memmove(mem+0x4000+0x8000-size, mem+0x4000, size);
	// mirroring of smaller cartridges
	if (size <= 0x1000) memcpy(mem+0xa000, mem+0xb000, 0x1000);
	if (size <= 0x2000) memcpy(mem+0x8000, mem+0xa000, 0x2000);
	if (size <= 0x4000)
	{
		const char *info;
		memcpy(&mem[0x4000], &mem[0x8000], 0x4000);
		info = hashfile_extrainfo(image);		
		if (strcmp(info, "") && !strcmp(info, "A13MIRRORING"))
		{
			memcpy(&mem[0x8000], &mem[0xa000], 0x2000);
			memcpy(&mem[0x6000], &mem[0x4000], 0x2000);
		}
	}
	logerror("%s loaded cartridge '%s' size %dK\n",
		image.device().machine->gamedrv->name, image.filename() , size/1024);
	return IMAGE_INIT_PASS;
}

DEVICE_IMAGE_UNLOAD( a5200_cart )
{
	UINT8 *mem = image.device().machine->region("maincpu")->base();
	/* zap the cartridge memory (again) */
	memset(&mem[0x4000], 0x00, 0x8000);
}

/*************************************
 *
 *  Atari XEGS
 *
 *************************************/

static UINT8 xegs_banks = 0;
static UINT8 xegs_cart = 0;

static WRITE8_HANDLER( xegs_bankswitch )
{
	UINT8 *cart = space->machine->region("user1")->base();
	data &= xegs_banks - 1;
	memory_set_bankptr(space->machine, "bank0", cart + data * 0x2000);
}

MACHINE_START( xegs )
{
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	UINT8 *cart = space->machine->region("user1")->base();
	UINT8 *cpu  = space->machine->region("maincpu")->base();

	atari_machine_start(machine);
	memory_install_write8_handler(space, 0xd500, 0xd5ff, 0, 0, xegs_bankswitch);

	if (xegs_cart)
	{
		memory_set_bankptr(machine, "bank0", cart);
		memory_set_bankptr(machine, "bank1", cart + (xegs_banks - 1) * 0x2000);
	}
	else
	{
		// point to built-in Missile Command (this does not work well, though... FIXME!!)
		memory_set_bankptr(machine, "bank0", cpu + 0x10000);
		memory_set_bankptr(machine, "bank1", cpu + 0x10000);
	}
}

DEVICE_IMAGE_LOAD( xegs_cart )
{
	UINT32 size;
	UINT8 *ptr = image.device().machine->region("user1")->base();

	if (image.software_entry() == NULL)
	{
		// skip the header
		image.fseek(0x10, SEEK_SET);
		size = image.length() - 0x10;
		if (image.fread(ptr, size) != size)
			return IMAGE_INIT_FAIL;
	}
	else
	{
		size = image.get_software_region_length("rom");
		memcpy(ptr, image.get_software_region("rom"), size);
	}

	xegs_banks = size / 0x2000;
	xegs_cart = 1;

	return IMAGE_INIT_PASS;
}

DEVICE_IMAGE_UNLOAD( xegs_cart )
{
	xegs_cart = 0;
	xegs_banks = 0;
}
