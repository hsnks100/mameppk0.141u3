/************************************************************

PC Engine CD HW notes:

CD Interface Register 0x00 - CDC status
x--- ---- busy signal
-x-- ---- request signal
---x ---- cd signal
---- x--- i/o signal

CD Interface Register 0x03 - BRAM lock / CD status
-x-- ---- acknowledge signal
--x- ---- done signal
---x ---- bram signal
---- x--- ADPCM 2
---- -x-- ADPCM 1
---- --x- CDDA left/right speaker select

CD Interface Register 0x05 - CD-DA Volume low 8-bit port

CD Interface Register 0x06 - CD-DA Volume high 8-bit port

CD Interface Register 0x07 - BRAM unlock / CD status
x--- ---- Enables BRAM

CD Interface Register 0x0c - ADPCM status
x--- ---- ADPCM is reading data
---- x--- ADPCM playback (0) stopped (1) currently playing
---- -x-- pending ADPCM data write
---- ---x ADPCM playback (1) stopped (0) currently playing

CD Interface Register 0x0d - ADPCM address control
x--- ---- ADPCM reset
-x-- ---- ADPCM play
--x- ---- ADPCM repeat
---x ---- ADPCM set length
---- x--- ADPCM set read address
---- --xx ADPCM set write address
(note: some games reads bit 5 and wants it to be low otherwise they hangs, surely NOT an ADPCM repeat flag read because it doesn't make sense)

CD Interface Register 0x0e - ADPCM playback rate

CD Interface Register 0x0f - ADPCM fade in/out register
---- xxxx command setting:
0x00 ADPCM/CD-DA Fade-in
0x01 CD-DA fade-in
0x08 CD-DA fade-out (short) ADPCM fade-in
0x09 CD-DA fade-out (long)
0x0a ADPCM fade-out (long)
0x0c CD-DA fade-out (short) ADPCM fade-in
0x0d CD-DA fade-out (short)
0x0e ADPCM fade-out (short)

*************************************************************/

#include "emu.h"
#include "coreutil.h"
#include "cpu/h6280/h6280.h"
#include "includes/pce.h"
#include "imagedev/chd_cd.h"
#include "sound/msm5205.h"
#include "sound/cdda.h"
#include "hashfile.h"


#define PCE_BRAM_SIZE				0x800
#define PCE_ADPCM_RAM_SIZE			0x10000
#define PCE_ACARD_RAM_SIZE			0x200000
#define PCE_CD_COMMAND_BUFFER_SIZE	0x100

#define PCE_CD_IRQ_TRANSFER_READY		0x40
#define PCE_CD_IRQ_TRANSFER_DONE		0x20
#define PCE_CD_IRQ_SAMPLE_FULL_PLAY		0x08
#define PCE_CD_IRQ_SAMPLE_HALF_PLAY		0x04

#define PCE_CD_ADPCM_PLAY_FLAG		0x08
#define PCE_CD_ADPCM_STOP_FLAG		0x01

#define PCE_CD_DATA_FRAMES_PER_SECOND	75

enum {
	PCE_CD_CDDA_OFF=0,
	PCE_CD_CDDA_PLAYING,
	PCE_CD_CDDA_PAUSED
};

static UINT8 pce_io_port_options;

/* system RAM */
#if 0 //def MESS
unsigned char *pce_user_ram;    /* scratch RAM at F8 */
#else
extern unsigned char *pce_user_ram;    /* scratch RAM at F8 */
#endif

/* CD Unit RAM */
UINT8 *pce_cd_ram;			/* 64KB RAM from a CD unit */
static UINT8	pce_sys3_card;	/* Is a Super CD System 3 card present */
static UINT8	pce_acard;		/* Is this an Arcade Card? */

typedef struct {
	UINT8	regs[16];
	UINT8	*bram;
	UINT8	*adpcm_ram;
	int		bram_locked;
	int		adpcm_read_ptr;
	UINT8	adpcm_read_buf;
	int		adpcm_write_ptr;
	UINT8	adpcm_write_buf;
	int		adpcm_length;
	int		adpcm_clock_divider;
	UINT32  msm_start_addr;
	UINT32	msm_end_addr;
	UINT32	msm_half_addr;
	UINT8	msm_nibble;
	UINT8	msm_idle;

	/* SCSI signals */
	int		scsi_BSY;	/* Busy. Bus in use */
	int		scsi_SEL;	/* Select. Initiator has won arbitration and has selected a target */
	int		scsi_CD;	/* Control/Data. Target is sending control (data) information */
	int		scsi_IO;	/* Input/Output. Target is sending (receiving) information */
	int		scsi_MSG;	/* Message. Target is sending or receiving a message */
	int		scsi_REQ;	/* Request. Target is requesting a data transfer */
	int		scsi_ACK;	/* Acknowledge. Initiator acknowledges that it is ready for a data transfer */
	int		scsi_ATN;	/* Attention. Initiator has a message ready for the target */
	int		scsi_RST;	/* Reset. Initiator forces all targets and any other initiators to do a warm reset */
	int		scsi_last_RST;	/* To catch setting of RST signal */
	int		cd_motor_on;
	int		selected;
	UINT8	*command_buffer;
	int		command_buffer_index;
	int		status_sent;
	int		message_after_status;
	int		message_sent;
	UINT8	*data_buffer;
	int		data_buffer_size;
	int		data_buffer_index;
	int		data_transferred;

	/* Arcade Card specific */
	UINT8	*acard_ram;
	UINT8	acard_latch;
	UINT8	acard_ctrl[4];
	UINT32	acard_base_addr[4];
	UINT16	acard_addr_offset[4];
	UINT16	acard_addr_inc[4];
	UINT32	acard_shift;
	UINT8	acard_shift_reg;

	UINT32	current_frame;
	UINT32	end_frame;
	UINT32	last_frame;
	UINT8	cdda_status;
	UINT8	cdda_play_mode;
	UINT8	*subcode_buffer;
	UINT8	end_mark;
	cdrom_file	*cd;
	const cdrom_toc*	toc;
	emu_timer	*data_timer;
	emu_timer	*adpcm_dma_timer;

	emu_timer	*cdda_fadeout_timer;
	emu_timer	*cdda_fadein_timer;
	double	cdda_volume;
	emu_timer	*adpcm_fadeout_timer;
	emu_timer	*adpcm_fadein_timer;
	double	adpcm_volume;
} pce_cd_t;
static pce_cd_t pce_cd;

/* MSM5205 ADPCM decoder definition */
static void pce_cd_msm5205_int(device_t *device);
const msm5205_interface pce_cd_msm5205_interface = {
	pce_cd_msm5205_int,	/* interrupt function */
	MSM5205_S48_4B		/* 1/48 prescaler, 4bit data */
};

static UINT8 *cartridge_ram;

/* joystick related data*/

#define JOY_CLOCK   0x01
#define JOY_RESET   0x02

#ifdef MESS
static int joystick_port_select;        /* internal index of joystick ports */
static int joystick_data_select;        /* which nibble of joystick data we want */
#endif

/* prototypes */
static void pce_cd_init( running_machine *machine );
static void pce_cd_set_irq_line( running_machine *machine, int num, int state );
static TIMER_CALLBACK( pce_cd_adpcm_dma_timer_callback );
static TIMER_CALLBACK( pce_cd_cdda_fadeout_callback );
static TIMER_CALLBACK( pce_cd_cdda_fadein_callback );
static TIMER_CALLBACK( pce_cd_adpcm_fadeout_callback );
static TIMER_CALLBACK( pce_cd_adpcm_fadein_callback );

static WRITE8_HANDLER( pce_sf2_banking_w )
{
	memory_set_bankptr( space->machine, "bank2", space->machine->region("user1")->base() + offset * 0x080000 + 0x080000 );
	memory_set_bankptr( space->machine, "bank3", space->machine->region("user1")->base() + offset * 0x080000 + 0x088000 );
	memory_set_bankptr( space->machine, "bank4", space->machine->region("user1")->base() + offset * 0x080000 + 0x0D0000 );
}

static WRITE8_HANDLER( pce_cartridge_ram_w )
{
	cartridge_ram[offset] = data;
}

DEVICE_IMAGE_LOAD(pce_cart)
{
	UINT32 size;
	int split_rom = 0, offset = 0;
	const char *extrainfo = NULL;
	unsigned char *ROM;
	logerror("*** DEVICE_IMAGE_LOAD(pce_cart) : %s\n", image.filename());

	/* open file to get size */
	ROM = image.device().machine->region("user1")->base();

	if (image.software_entry() == NULL)
		size = image.length();
	else
		size = image.get_software_region_length("rom");

	/* handle header accordingly */
	if ((size / 512) & 1)
	{
		logerror("*** DEVICE_IMAGE_LOAD(pce_cart) : Header present\n");
		size -= 512;
		offset = 512;
	}

	if (size > PCE_ROM_MAXSIZE)
		size = PCE_ROM_MAXSIZE;

	if (image.software_entry() == NULL)
	{
		image.fseek(offset, SEEK_SET);
		image.fread( ROM, size);
		extrainfo = hashfile_extrainfo(image);
	}
	else
		memcpy(ROM, image.get_software_region("rom") + offset, size);

	if (extrainfo)
	{
		logerror("extrainfo: %s\n", extrainfo);
		if (strstr(extrainfo, "ROM_SPLIT"))
			split_rom = 1;
	}

	if (ROM[0x1fff] < 0xe0)
	{
		int i;
		UINT8 decrypted[256];

		logerror( "*** DEVICE_IMAGE_LOAD(pce_cart) : ROM image seems encrypted, decrypting...\n" );

		/* Initialize decryption table */
		for (i = 0; i < 256; i++)
			decrypted[i] = ((i & 0x01) << 7) | ((i & 0x02) << 5) | ((i & 0x04) << 3) | ((i & 0x08) << 1) | ((i & 0x10) >> 1) | ((i & 0x20 ) >> 3) | ((i & 0x40) >> 5) | ((i & 0x80) >> 7);

		/* Decrypt ROM image */
		for (i = 0; i < size; i++)
			ROM[i] = decrypted[ROM[i]];
	}

	/* check if we're dealing with a split rom image */
	if (size == 384 * 1024)
	{
		split_rom = 1;
		/* Mirror the upper 128KB part of the image */
		memcpy(ROM + 0x060000, ROM + 0x040000, 0x020000);	/* Set up 060000 - 07FFFF mirror */
	}

	/* set up the memory for a split rom image */
	if (split_rom)
	{
		logerror("Split rom detected, setting up memory accordingly\n");
		/* Set up ROM address space as follows:          */
		/* 000000 - 03FFFF : ROM data 000000 - 03FFFF    */
		/* 040000 - 07FFFF : ROM data 000000 - 03FFFF    */
		/* 080000 - 0BFFFF : ROM data 040000 - 07FFFF    */
		/* 0C0000 - 0FFFFF : ROM data 040000 - 07FFFF    */
		memcpy(ROM + 0x080000, ROM + 0x040000, 0x040000);	/* Set up 080000 - 0BFFFF region */
		memcpy(ROM + 0x0C0000, ROM + 0x040000, 0x040000);	/* Set up 0C0000 - 0FFFFF region */
		memcpy(ROM + 0x040000, ROM, 0x040000);		/* Set up 040000 - 07FFFF region */
	}
	else
	{
		/* mirror 256KB rom data */
		if (size <= 0x040000)
			memcpy(ROM + 0x040000, ROM, 0x040000);

		/* mirror 512KB rom data */
		if (size <= 0x080000)
			memcpy(ROM + 0x080000, ROM, 0x080000);
	}

	memory_set_bankptr(image.device().machine, "bank1", ROM);
	memory_set_bankptr(image.device().machine, "bank2", ROM + 0x080000);
	memory_set_bankptr(image.device().machine, "bank3", ROM + 0x088000);
	memory_set_bankptr(image.device().machine, "bank4", ROM + 0x0d0000);

	/* Check for Street fighter 2 */
	if (size == PCE_ROM_MAXSIZE)
	{
		memory_install_write8_handler(cputag_get_address_space(image.device().machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x01ff0, 0x01ff3, 0, 0, pce_sf2_banking_w);
	}

	/* Check for Populous */
	if (!memcmp(ROM + 0x1F26, "POPULOUS", 8))
	{
		cartridge_ram = auto_alloc_array(image.device().machine, UINT8, 0x8000);
		memory_set_bankptr(image.device().machine, "bank2", cartridge_ram);
		memory_install_write8_handler(cputag_get_address_space(image.device().machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x080000, 0x087FFF, 0, 0, pce_cartridge_ram_w);
	}

	/* Check for CD system card */
	pce_sys3_card = 0;
	if (!memcmp(ROM + 0x3FFB6, "PC Engine CD-ROM SYSTEM", 23))
	{
		/* Check if 192KB additional system card ram should be used */
		if(!memcmp(ROM + 0x29D1, "VER. 3.", 7)) 		{ pce_sys3_card = 1; } // JP version
		else if(!memcmp(ROM + 0x29C4, "VER. 3.", 7 ))	{ pce_sys3_card = 3; } // US version

		if(pce_sys3_card)
		{
			cartridge_ram = auto_alloc_array(image.device().machine, UINT8, 0x30000);
			memory_set_bankptr(image.device().machine, "bank4", cartridge_ram);
			memory_install_write8_handler(cputag_get_address_space(image.device().machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x0D0000, 0x0FFFFF, 0, 0, pce_cartridge_ram_w);
			memory_install_readwrite8_handler(cputag_get_address_space(image.device().machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x080000, 0x087FFF, 0, 0, pce_cd_acard_wram_r,pce_cd_acard_wram_w);
		}
	}
	return 0;
}
#ifdef MESS
DRIVER_INIT( pce_mess )
{
	pce_io_port_options = PCE_JOY_SIG | CONST_SIG;
}
#endif
DRIVER_INIT( tg16 )
{
	pce_io_port_options = TG_16_JOY_SIG | CONST_SIG;
}

DRIVER_INIT( sgx )
{
	pce_io_port_options = PCE_JOY_SIG | CONST_SIG;
}

MACHINE_START( pce )
{
	pce_cd_init( machine );
}

#ifdef MESS
static UINT8 joy_6b_packet[5];

MACHINE_RESET( pce_mess )
{
	int joy_i;

	for (joy_i = 0; joy_i < 5; joy_i++)
		joy_6b_packet[joy_i] = 0;

	pce_cd.adpcm_read_buf = 0;
	pce_cd.adpcm_write_buf = 0;

	// TODO: add CD-DA stop command here

	pce_cd.regs[0x0c] |= PCE_CD_ADPCM_STOP_FLAG;
	pce_cd.regs[0x0c] &= ~PCE_CD_ADPCM_PLAY_FLAG;
	//pce_cd.regs[0x03] = (pce_cd.regs[0x03] & ~0x0c) | (PCE_CD_SAMPLE_STOP_PLAY);

	/* Note: Arcade Card BIOS contents are the same as System 3, only internal HW differs.
       We use a category to select between modes (some games can be run in either S-CD or A-CD modes) */
	pce_acard = input_port_read(machine, "A_CARD") & 1;
}

/* todo: how many input ports does the PCE have? */
WRITE8_HANDLER ( pce_mess_joystick_w )
{
	int joy_i;
	UINT8 joy_type = input_port_read(space->machine,"JOY_TYPE");

	h6280io_set_buffer(space->cpu, data);

    /* bump counter on a low-to-high transition of bit 1 */
    if ((!joystick_data_select) && (data & JOY_CLOCK))
    {
        joystick_port_select = (joystick_port_select + 1) & 0x07;
    }

    /* do we want buttons or direction? */
    joystick_data_select = data & JOY_CLOCK;

    /* clear counter if bit 2 is set */
    if (data & JOY_RESET)
    {
		joystick_port_select = 0;

		for (joy_i = 0; joy_i < 5; joy_i++)
		{
			if (((joy_type >> (joy_i*2)) & 3) == 2)
        		joy_6b_packet[joy_i] ^= 1;
		}
    }
}

READ8_HANDLER ( pce_mess_joystick_r )
{
	static const char *const joyname[4][5] = {
		{ "JOY_P1", "JOY_P2", "JOY_P3", "JOY_P4", "JOY_P5" },
		{ },
		{ "JOY6B_P1", "JOY6B_P2", "JOY6B_P3", "JOY6B_P4", "JOY6B_P5" },
		{ }
	};
	UINT8 joy_type = input_port_read(space->machine, "JOY_TYPE");
	UINT8 ret, data;

	if (joystick_port_select <= 4)
	{
		switch((joy_type >> (joystick_port_select*2)) & 3)
		{
			case 0: //2-buttons pad
				data = input_port_read(space->machine, joyname[0][joystick_port_select]);
				break;
			case 2: //6-buttons pad
				/*
                Two packets:
                1st packet: directions + I, II, Run, Select
                2nd packet: 6 buttons "header" (high 4 bits active low) + III, IV, V, VI
                Note that six buttons pad just doesn't work with (almost?) every single 2-button-only games, it's really just an after-thought and it is like this
                on real HW.
                */
				data = input_port_read(space->machine, joyname[2][joystick_port_select]) >> (joy_6b_packet[joystick_port_select]*8);
				break;
			default:
				data = 0xff;
				break;
		}
	}
	else
		data = 0xff;


	if (joystick_data_select)
		data >>= 4;

	ret = (data & 0x0f) | pce_io_port_options;
#ifdef UNIFIED_PCE
	ret &= ~0x40;
#endif

	return (ret);
}
#endif
NVRAM_HANDLER( pce )
{
	if (read_or_write)
	{
		file->write(pce_cd.bram, PCE_BRAM_SIZE);
	}
	else
	{
		/* load battery backed memory from disk */
		if (file)
			file->read(pce_cd.bram, PCE_BRAM_SIZE);
	}
}

static void pce_set_cd_bram( running_machine *machine )
{
	memory_set_bankptr( machine, "bank10", pce_cd.bram + ( pce_cd.bram_locked ? PCE_BRAM_SIZE : 0 ) );
}

static void adpcm_stop(running_machine *machine)
{
	pce_cd.regs[0x0c] |= PCE_CD_ADPCM_STOP_FLAG;
	pce_cd.regs[0x0c] &= ~PCE_CD_ADPCM_PLAY_FLAG;
	//pce_cd.regs[0x03] = (pce_cd.regs[0x03] & ~0x0c) | (PCE_CD_SAMPLE_STOP_PLAY);
	pce_cd_set_irq_line( machine, PCE_CD_IRQ_SAMPLE_FULL_PLAY, ASSERT_LINE );
	pce_cd.regs[0x0d] &= ~0x60;
	pce_cd.msm_idle = 1;
}

static void adpcm_play(running_machine *machine)
{
	pce_cd.regs[0x0c] &= ~PCE_CD_ADPCM_STOP_FLAG;
	pce_cd.regs[0x0c] |= PCE_CD_ADPCM_PLAY_FLAG;
	pce_cd_set_irq_line( machine, PCE_CD_IRQ_SAMPLE_FULL_PLAY, CLEAR_LINE );
	pce_cd.regs[0x03] = (pce_cd.regs[0x03] & ~0x0c);
	pce_cd.msm_idle = 0;
}


/* Callback for new data from the MSM5205.
  The PCE cd unit actually divides the clock signal supplied to
  the MSM5205. Currently we can only use static clocks for the
  MSM5205.
 */
static void pce_cd_msm5205_int(device_t *device)
{
	UINT8 msm_data;

//  popmessage("%08x %08x %08x %02x %02x",pce_cd.msm_start_addr,pce_cd.msm_end_addr,pce_cd.msm_half_addr,pce_cd.regs[0x0c],pce_cd.regs[0x0d]);

	if ( pce_cd.msm_idle )
		return;

	/* Supply new ADPCM data */
	msm_data = (pce_cd.msm_nibble) ? (pce_cd.adpcm_ram[pce_cd.msm_start_addr] & 0x0f) : ((pce_cd.adpcm_ram[pce_cd.msm_start_addr] & 0xf0) >> 4);

	msm5205_data_w(device, msm_data);
	pce_cd.msm_nibble ^= 1;

	if(pce_cd.msm_nibble == 0)
	{
		pce_cd.msm_start_addr++;

		if(pce_cd.msm_start_addr == pce_cd.msm_half_addr)
		{
			//pce_cd_set_irq_line( device->machine, PCE_CD_IRQ_SAMPLE_FULL_PLAY, CLEAR_LINE );
			//pce_cd_set_irq_line( device->machine, PCE_CD_IRQ_SAMPLE_HALF_PLAY, ASSERT_LINE );
		}

		if(pce_cd.msm_start_addr > pce_cd.msm_end_addr)
		{
			//pce_cd_set_irq_line( device->machine, PCE_CD_IRQ_SAMPLE_HALF_PLAY, CLEAR_LINE );
			//pce_cd_set_irq_line( device->machine, PCE_CD_IRQ_SAMPLE_FULL_PLAY, CLEAR_LINE );
			adpcm_stop(device->machine);
			msm5205_reset_w(device, 1);
		}
	}
}

#define	SCSI_STATUS_OK			0x00
#define SCSI_CHECK_CONDITION	0x02

static void pce_cd_reply_status_byte( UINT8 status )
{
logerror("Setting CD in reply_status_byte\n");
	pce_cd.scsi_CD = pce_cd.scsi_IO = pce_cd.scsi_REQ = 1;
	pce_cd.scsi_MSG = 0;
	pce_cd.message_after_status = 1;
	pce_cd.status_sent = pce_cd.message_sent = 0;

	if ( status == SCSI_STATUS_OK )
	{
		pce_cd.regs[0x01] = 0x00;
	}
	else
	{
		pce_cd.regs[0x01] = 0x01;
	}
}

/* 0x00 - TEST UNIT READY */
static void pce_cd_test_unit_ready( running_machine *machine )
{
	logerror("test unit ready\n");
	if ( pce_cd.cd )
	{
		logerror( "Sending STATUS_OK status\n" );
		pce_cd_reply_status_byte( SCSI_STATUS_OK );
	}
	else
	{
		logerror( "Sending CHECK_CONDITION status\n" );
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
	}
}

/* 0x08 - READ (6) */
static void pce_cd_read_6( running_machine *machine )
{
	UINT32 frame = ( ( pce_cd.command_buffer[1] & 0x1F ) << 16 ) | ( pce_cd.command_buffer[2] << 8 ) | pce_cd.command_buffer[3];
	UINT32 frame_count = pce_cd.command_buffer[4];

	/* Check for presence of a CD */
	if ( ! pce_cd.cd )
	{
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	if ( pce_cd.cdda_status != PCE_CD_CDDA_OFF )
	{
		pce_cd.cdda_status = PCE_CD_CDDA_OFF;
		cdda_stop_audio( machine->device( "cdda" ) );
		pce_cd.end_mark = 0;
	}

	pce_cd.current_frame = frame;
	pce_cd.end_frame = frame + frame_count;

	if ( frame_count == 0 )
	{
		/* Star Breaker uses this */
		popmessage("Read Sector frame count == 0, contact MESSdev");
		pce_cd_reply_status_byte( SCSI_STATUS_OK );
	}
	else
	{
		pce_cd.data_timer->adjust(attotime::from_hz( PCE_CD_DATA_FRAMES_PER_SECOND ), 0, attotime::from_hz( PCE_CD_DATA_FRAMES_PER_SECOND ));
	}
}

/* 0xD8 - SET AUDIO PLAYBACK START POSITION (NEC) */
static void pce_cd_nec_set_audio_start_position( running_machine *machine )
{
	UINT32	frame = 0;

	if ( ! pce_cd.cd )
	{
		/* Throw some error here */
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	switch( pce_cd.command_buffer[9] & 0xC0 )
	{
	case 0x00:
		popmessage("CD-DA set start mode 0x00, contact MESSdev");
		frame = ( pce_cd.command_buffer[3] << 16 ) | ( pce_cd.command_buffer[4] << 8 ) | pce_cd.command_buffer[5];
		break;
	case 0x40:
	{
		UINT8 m,s,f;

		m = bcd_2_dec( pce_cd.command_buffer[2]);
		s = bcd_2_dec( pce_cd.command_buffer[3]);
		f = bcd_2_dec( pce_cd.command_buffer[4]);

		frame = f + 75 * (s + m * 60);
		if(frame >= 525) // TODO: seven seconds gap? O_o
			frame -= 525;
		break;
	}
	case 0x80:
		frame = pce_cd.toc->tracks[ bcd_2_dec( pce_cd.command_buffer[2] ) - 1 ].physframeofs;
		break;
	default:
		popmessage("CD-DA set start mode 0xc0, contact MESSdev");
		//assert( NULL == pce_cd_nec_set_audio_start_position );
		break;
	}

	pce_cd.current_frame = frame;

	if ( pce_cd.cdda_status == PCE_CD_CDDA_PAUSED )
	{
		pce_cd.cdda_status = PCE_CD_CDDA_OFF;
		cdda_stop_audio( machine->device( "cdda" ) );
		pce_cd.end_frame = pce_cd.last_frame;
		pce_cd.end_mark = 0;
	}
	else
	{
		if(pce_cd.command_buffer[1] & 0x03)
		{
			pce_cd.cdda_status = PCE_CD_CDDA_PLAYING;
			pce_cd.end_frame = pce_cd.last_frame; //get the end of the CD
			cdda_start_audio( machine->device( "cdda" ), pce_cd.current_frame, pce_cd.end_frame - pce_cd.current_frame );
			pce_cd.cdda_play_mode = (pce_cd.command_buffer[1] & 0x02) ? 2 : 3; // mode 2 sets IRQ at end
			pce_cd.end_mark =  (pce_cd.command_buffer[1] & 0x02) ? 1 : 0;
		}
		else
		{
			pce_cd.cdda_status = PCE_CD_CDDA_PLAYING;
			pce_cd.end_frame = pce_cd.toc->tracks[ cdrom_get_track(pce_cd.cd, pce_cd.current_frame) + 1 ].physframeofs; //get the end of THIS track
			cdda_start_audio( machine->device( "cdda" ), pce_cd.current_frame, pce_cd.end_frame - pce_cd.current_frame );
			pce_cd.end_mark = 0;
			pce_cd.cdda_play_mode = 3;
		}
	}

	pce_cd_reply_status_byte( SCSI_STATUS_OK );
	pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_DONE, ASSERT_LINE );
}

/* 0xD9 - SET AUDIO PLAYBACK END POSITION (NEC) */
static void pce_cd_nec_set_audio_stop_position( running_machine *machine )
{
	UINT32  frame = 0;

	if ( ! pce_cd.cd )
	{
		/* Throw some error here */
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	switch( pce_cd.command_buffer[9] & 0xC0 )
	{
	case 0x00:
		popmessage("CD-DA set end mode 0x00, contact MESSdev");
		frame = ( pce_cd.command_buffer[3] << 16 ) | ( pce_cd.command_buffer[4] << 8 ) | pce_cd.command_buffer[5];
		break;
	case 0x40:
	{
		UINT8 m,s,f;

		m = bcd_2_dec( pce_cd.command_buffer[2]);
		s = bcd_2_dec( pce_cd.command_buffer[3]);
		f = bcd_2_dec( pce_cd.command_buffer[4]);

		frame = f + 75 * (s + m * 60);
		if(frame >= 525) // TODO: seven seconds gap? O_o
			frame -= 525;
		break;
	}
	case 0x80:
		frame = pce_cd.toc->tracks[ bcd_2_dec( pce_cd.command_buffer[2] ) - 1 ].physframeofs;
		break;
	default:
		popmessage("CD-DA set end mode 0xc0, contact MESSdev");
		//assert( NULL == pce_cd_nec_set_audio_start_position );
		break;
	}

	pce_cd.end_frame = frame;
	pce_cd.cdda_play_mode = pce_cd.command_buffer[1] & 0x03;

	if ( pce_cd.cdda_play_mode )
	{
		if ( pce_cd.cdda_status == PCE_CD_CDDA_PAUSED )
		{
			cdda_pause_audio( machine->device( "cdda" ), 0 );
		}
		else
		{
			cdda_start_audio( machine->device( "cdda" ), pce_cd.current_frame, pce_cd.end_frame - pce_cd.current_frame );
			pce_cd.end_mark = 1;
		}
		pce_cd.cdda_status = PCE_CD_CDDA_PLAYING;
	}
	else
	{
		pce_cd.cdda_status = PCE_CD_CDDA_OFF;
		cdda_stop_audio( machine->device( "cdda" ) );
		pce_cd.end_frame = pce_cd.last_frame;
		pce_cd.end_mark = 0;
//      assert( NULL == pce_cd_nec_set_audio_stop_position );
	}

	pce_cd_reply_status_byte( SCSI_STATUS_OK );
	pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_DONE, ASSERT_LINE );
}

/* 0xDA - PAUSE (NEC) */
static void pce_cd_nec_pause( running_machine *machine )
{

	/* If no cd mounted throw an error */
	if ( ! pce_cd.cd )
	{
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	/* If there was no cdda playing, throw an error */
	if ( pce_cd.cdda_status == PCE_CD_CDDA_OFF )
	{
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	pce_cd.cdda_status = PCE_CD_CDDA_PAUSED;
	pce_cd.current_frame = cdda_get_audio_lba( machine->device( "cdda" ) );
	cdda_pause_audio( machine->device( "cdda" ), 1 );
	pce_cd_reply_status_byte( SCSI_STATUS_OK );
}

/* 0xDD - READ SUBCHANNEL Q (NEC) */
static void pce_cd_nec_get_subq( running_machine *machine )
{
	/* WP - I do not have access to chds with subchannel information yet, so I'm faking something here */
	UINT32 msf_abs, msf_rel, track, frame;

	if ( ! pce_cd.cd )
	{
		/* Throw some error here */
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
		return;
	}

	frame = pce_cd.current_frame;

	switch( pce_cd.cdda_status )
	{
	case PCE_CD_CDDA_PAUSED:
		pce_cd.data_buffer[0] = 2;
		frame = cdda_get_audio_lba( machine->device( "cdda" ) );
		break;
	case PCE_CD_CDDA_PLAYING:
		pce_cd.data_buffer[0] = 0;
		frame = cdda_get_audio_lba( machine->device( "cdda" ) );
		break;
	default:
		pce_cd.data_buffer[0] = 3;
		break;
	}

	msf_abs = lba_to_msf( frame );
	track = cdrom_get_track( pce_cd.cd, frame );
	msf_rel = lba_to_msf( frame - cdrom_get_track_start( pce_cd.cd, track ) );

	pce_cd.data_buffer[1] = 0;
	pce_cd.data_buffer[2] = dec_2_bcd( track+1 );		/* track */
	pce_cd.data_buffer[3] = 1;							/* index */
	pce_cd.data_buffer[4] = ( msf_rel >> 16 ) & 0xFF;	/* M (relative) */
	pce_cd.data_buffer[5] = ( msf_rel >> 8 ) & 0xFF;	/* S (relative) */
	pce_cd.data_buffer[6] = msf_rel & 0xFF;;			/* F (relative) */
	pce_cd.data_buffer[7] = ( msf_abs >> 16 ) & 0xFF;	/* M (absolute) */
	pce_cd.data_buffer[8] = ( msf_abs >> 8 ) & 0xFF;	/* S (absolute) */
	pce_cd.data_buffer[9] = msf_abs & 0xFF;				/* F (absolute) */
	pce_cd.data_buffer_size = 10;

	pce_cd.data_buffer_index = 0;
	pce_cd.data_transferred = 1;
	pce_cd.scsi_IO = 1;
	pce_cd.scsi_CD = 0;
}

/* 0xDE - GET DIR INFO (NEC) */
static void pce_cd_nec_get_dir_info( running_machine *machine )
{
	UINT32 frame, msf, track = 0;
	const cdrom_toc	*toc;
	logerror("nec get dir info\n");

	if ( ! pce_cd.cd )
	{
		/* Throw some error here */
		pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
	}

	toc = cdrom_get_toc( pce_cd.cd );

	switch( pce_cd.command_buffer[1] )
	{
	case 0x00:		/* Get first and last track numbers */
		pce_cd.data_buffer[0] = dec_2_bcd(1);
		pce_cd.data_buffer[1] = dec_2_bcd(toc->numtrks);
		pce_cd.data_buffer_size = 2;
		break;
	case 0x01:		/* Get total disk size in MSF format */
		frame = toc->tracks[toc->numtrks-1].physframeofs;
		frame += toc->tracks[toc->numtrks-1].frames;
		msf = lba_to_msf( frame + 150 );

		pce_cd.data_buffer[0] = ( msf >> 16 ) & 0xFF;	/* M */
		pce_cd.data_buffer[1] = ( msf >> 8 ) & 0xFF;	/* S */
		pce_cd.data_buffer[2] = msf & 0xFF;				/* F */
		pce_cd.data_buffer_size = 3;
		break;
	case 0x02:		/* Get track information */
		if ( pce_cd.command_buffer[2] == 0xAA )
		{
			frame = toc->tracks[toc->numtrks-1].physframeofs;
			frame += toc->tracks[toc->numtrks-1].frames;
			pce_cd.data_buffer[3] = 0x04;	/* correct? */
		} else
		{
			track = MAX( bcd_2_dec( pce_cd.command_buffer[2] ), 1 );
			frame = toc->tracks[track-1].physframeofs;
			pce_cd.data_buffer[3] = ( toc->tracks[track-1].trktype == CD_TRACK_AUDIO ) ? 0x00 : 0x04;
		}
		logerror("track = %d, frame = %d\n", track, frame );
		msf = lba_to_msf( frame + 150 );
		pce_cd.data_buffer[0] = ( msf >> 16 ) & 0xFF;	/* M */
		pce_cd.data_buffer[1] = ( msf >> 8 ) & 0xFF;	/* S */
		pce_cd.data_buffer[2] = msf & 0xFF;				/* F */
		pce_cd.data_buffer_size = 4;
		break;
	default:
//      assert( pce_cd_nec_get_dir_info == NULL );  // Not implemented yet
		break;
	}

	pce_cd.data_buffer_index = 0;
	pce_cd.data_transferred = 1;
	pce_cd.scsi_IO = 1;
	pce_cd.scsi_CD = 0;
}

static void pce_cd_end_of_list( running_machine *machine )
{
	pce_cd_reply_status_byte( SCSI_CHECK_CONDITION );
}

static void pce_cd_handle_data_output( running_machine *machine )
{
	static const struct {
		UINT8	command_byte;
		UINT8	command_size;
		void	(*command_handler)(running_machine *machine);
	} pce_cd_commands[] = {
		{ 0x00, 6, pce_cd_test_unit_ready },				/* TEST UNIT READY */
		{ 0x08, 6, pce_cd_read_6 },							/* READ (6) */
		{ 0xD8,10, pce_cd_nec_set_audio_start_position },	/* NEC SET AUDIO PLAYBACK START POSITION */
		{ 0xD9,10, pce_cd_nec_set_audio_stop_position },	/* NEC SET AUDIO PLAYBACK END POSITION */
		{ 0xDA,10, pce_cd_nec_pause },						/* NEC PAUSE */
		{ 0xDD,10, pce_cd_nec_get_subq },					/* NEC GET SUBCHANNEL Q */
		{ 0xDE,10, pce_cd_nec_get_dir_info },				/* NEC GET DIR INFO */
		{ 0xFF, 1, pce_cd_end_of_list }						/* end of list marker */
	};

	if ( pce_cd.scsi_REQ && pce_cd.scsi_ACK )
	{
		/* Command byte received */
		logerror( "Command byte $%02X received\n", pce_cd.regs[0x01] );

		/* Check for buffer overflow */
		assert( pce_cd.command_buffer_index < PCE_CD_COMMAND_BUFFER_SIZE );

		pce_cd.command_buffer[pce_cd.command_buffer_index] = pce_cd.regs[0x01];
		pce_cd.command_buffer_index++;
		pce_cd.scsi_REQ = 0;
	}

	if ( ! pce_cd.scsi_REQ && ! pce_cd.scsi_ACK && pce_cd.command_buffer_index )
	{
		int i = 0;

		logerror( "Check if command done\n" );

		for( i = 0; pce_cd.command_buffer[0] > pce_cd_commands[i].command_byte; i++ );

		/* Check for unknown commands */
		if ( pce_cd.command_buffer[0] != pce_cd_commands[i].command_byte )
		{
			logerror("Unrecognized command: %02X\n", pce_cd.command_buffer[0] );
			if(pce_cd.command_buffer[0] == 0x03)
				popmessage("CD command 0x03 issued (Request Sense), contact MESSdev");
		}
		assert( pce_cd.command_buffer[0] == pce_cd_commands[i].command_byte );

		if ( pce_cd.command_buffer_index == pce_cd_commands[i].command_size )
		{
			(pce_cd_commands[i].command_handler)( machine );
			pce_cd.command_buffer_index = 0;
		}
		else
		{
			pce_cd.scsi_REQ = 1;
		}
	}
}

static void pce_cd_handle_data_input( running_machine *machine )
{
	if ( pce_cd.scsi_CD )
	{
		/* Command / Status byte */
		if ( pce_cd.scsi_REQ && pce_cd.scsi_ACK )
		{
			logerror( "status sent\n" );
			pce_cd.scsi_REQ = 0;
			pce_cd.status_sent = 1;
		}

		if ( ! pce_cd.scsi_REQ && ! pce_cd.scsi_ACK && pce_cd.status_sent )
		{
			pce_cd.status_sent = 0;
			if ( pce_cd.message_after_status )
			{
				logerror( "message after status\n" );
				pce_cd.message_after_status = 0;
				pce_cd.scsi_MSG = pce_cd.scsi_REQ = 1;
				pce_cd.regs[0x01] = 0;
			}
		}
	}
	else
	{
		/* Data */
		if ( pce_cd.scsi_REQ && pce_cd.scsi_ACK )
		{
			pce_cd.scsi_REQ = 0;
		}

		if ( ! pce_cd.scsi_REQ && ! pce_cd.scsi_ACK )
		{
			if ( pce_cd.data_buffer_index == pce_cd.data_buffer_size )
			{
				pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_READY, CLEAR_LINE );
				if ( pce_cd.data_transferred )
				{
					pce_cd.data_transferred = 0;
					pce_cd_reply_status_byte( SCSI_STATUS_OK );
					pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_DONE, ASSERT_LINE );
				}
			}
			else
			{
				logerror("Transfer byte from offset %d\n", pce_cd.data_buffer_index);
				pce_cd.regs[0x01] = pce_cd.data_buffer[pce_cd.data_buffer_index];
				pce_cd.data_buffer_index++;
				pce_cd.scsi_REQ = 1;
			}
		}
	}
}

static void pce_cd_handle_message_output( void )
{
	if ( pce_cd.scsi_REQ && pce_cd.scsi_ACK )
	{
		pce_cd.scsi_REQ = 0;
	}
}

static void pce_cd_handle_message_input( void )
{
	if ( pce_cd.scsi_REQ && pce_cd.scsi_ACK )
	{
		pce_cd.scsi_REQ = 0;
		pce_cd.message_sent = 1;
	}

	if ( ! pce_cd.scsi_REQ && ! pce_cd.scsi_ACK && pce_cd.message_sent )
	{
		pce_cd.message_sent = 0;
		pce_cd.scsi_BSY = 0;
	}
}

/* Update internal CD statuses */
static void pce_cd_update( running_machine *machine )
{
	/* Check for reset of CD unit */
	if ( pce_cd.scsi_RST != pce_cd.scsi_last_RST )
	{
		if ( pce_cd.scsi_RST )
		{
			logerror("Performing CD reset\n");
			/* Reset internal data */
			pce_cd.scsi_BSY = pce_cd.scsi_SEL = pce_cd.scsi_CD = pce_cd.scsi_IO = 0;
			pce_cd.scsi_MSG = pce_cd.scsi_REQ = pce_cd.scsi_ATN = 0;
			pce_cd.cd_motor_on = 0;
			pce_cd.selected = 0;
			pce_cd.cdda_status = PCE_CD_CDDA_OFF;
			cdda_stop_audio( machine->device( "cdda" ) );
			pce_cd.adpcm_dma_timer->adjust(attotime::never); // stop ADPCM DMA here
		}
		pce_cd.scsi_last_RST = pce_cd.scsi_RST;
	}

	/* Check if bus can be freed */
	if ( ! pce_cd.scsi_SEL && ! pce_cd.scsi_BSY && pce_cd.selected )
	{
		logerror( "freeing bus\n" );
		pce_cd.selected = 0;
		pce_cd.scsi_CD = pce_cd.scsi_MSG = pce_cd.scsi_IO = pce_cd.scsi_REQ = 0;
		pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_DONE, CLEAR_LINE );
	}

	/* Select the CD device */
	if ( pce_cd.scsi_SEL )
	{
		if ( ! pce_cd.selected )
		{
			pce_cd.selected = 1;
			logerror("Setting CD in device selection\n");
			pce_cd.scsi_BSY = pce_cd.scsi_REQ = pce_cd.scsi_CD = 1;
			pce_cd.scsi_MSG = pce_cd.scsi_IO = 0;
		}
	}

	if ( pce_cd.scsi_ATN )
	{
	}
	else
	{
		/* Check for data and pessage phases */
		if ( pce_cd.scsi_BSY )
		{
			if ( pce_cd.scsi_MSG )
			{
				/* message phase */
				if ( pce_cd.scsi_IO )
				{
					pce_cd_handle_message_input();
				}
				else
				{
					pce_cd_handle_message_output();
				}
			}
			else
			{
				/* data phase */
				if ( pce_cd.scsi_IO )
				{
					/* Reading data from target */
					pce_cd_handle_data_input( machine );
				}
				else
				{
					/* Sending data to target */
					pce_cd_handle_data_output( machine );
				}
			}
		}
	}

	/* FIXME: presumably CD-DA needs an irq interface for this */
	if(cdda_audio_ended(machine->device("cdda")) && pce_cd.end_mark == 1)
	{
		switch(pce_cd.cdda_play_mode & 3)
		{
			case 1: cdda_start_audio( machine->device( "cdda" ), pce_cd.current_frame, pce_cd.end_frame - pce_cd.current_frame ); pce_cd.end_mark = 1; break; //play with repeat
			case 2: pce_cd_set_irq_line( machine, PCE_CD_IRQ_TRANSFER_DONE, ASSERT_LINE ); pce_cd.end_mark = 0; break; //irq when finished
			case 3: pce_cd.end_mark = 0; break; //play without repeat
		}
	}
}

static void pce_cd_set_irq_line( running_machine *machine, int num, int state )
{
	switch( num )
	{
	case PCE_CD_IRQ_TRANSFER_DONE:
		if ( state == ASSERT_LINE )
		{
			pce_cd.regs[0x03] |= PCE_CD_IRQ_TRANSFER_DONE;
		}
		else
		{
			pce_cd.regs[0x03] &= ~ PCE_CD_IRQ_TRANSFER_DONE;
		}
		break;
	case PCE_CD_IRQ_TRANSFER_READY:
		if ( state == ASSERT_LINE )
		{
			pce_cd.regs[0x03] |= PCE_CD_IRQ_TRANSFER_READY;
		}
		else
		{
			pce_cd.regs[0x03] &= ~ PCE_CD_IRQ_TRANSFER_READY;
		}
		break;
	case PCE_CD_IRQ_SAMPLE_FULL_PLAY:
		if ( state == ASSERT_LINE )
		{
			pce_cd.regs[0x03] |= PCE_CD_IRQ_SAMPLE_FULL_PLAY;
			//printf("x %02x %02x\n",pce_cd.regs[0x02],pce_cd.regs[0x03]);
		}
		else
		{
			pce_cd.regs[0x03] &= ~ PCE_CD_IRQ_SAMPLE_FULL_PLAY;
		}
		break;
	case PCE_CD_IRQ_SAMPLE_HALF_PLAY:
		if ( state == ASSERT_LINE )
		{
			pce_cd.regs[0x03] |= PCE_CD_IRQ_SAMPLE_HALF_PLAY;
			//printf("y %02x %02x\n",pce_cd.regs[0x02],pce_cd.regs[0x03]);
		}
		else
		{
			pce_cd.regs[0x03] &= ~ PCE_CD_IRQ_SAMPLE_HALF_PLAY;
		}
		break;
	default:
		break;
	}

	if ( pce_cd.regs[0x02] & pce_cd.regs[0x03] & ( PCE_CD_IRQ_TRANSFER_DONE | PCE_CD_IRQ_TRANSFER_READY | PCE_CD_IRQ_SAMPLE_HALF_PLAY | PCE_CD_IRQ_SAMPLE_FULL_PLAY) )
	{
		cputag_set_input_line(machine, "maincpu", 1, ASSERT_LINE );
	}
	else
	{
		cputag_set_input_line(machine, "maincpu", 1, CLEAR_LINE );
	}
}

static TIMER_CALLBACK( pce_cd_data_timer_callback )
{
	if ( pce_cd.data_buffer_index == pce_cd.data_buffer_size )
	{
		/* Read next data sector */
		logerror("read sector %d\n", pce_cd.current_frame );
		if ( ! cdrom_read_data( pce_cd.cd, pce_cd.current_frame, pce_cd.data_buffer, CD_TRACK_MODE1 ) )
		{
			logerror("Mode1 CD read failed for frame #%d\n", pce_cd.current_frame );
		}
		else
		{
			logerror("Succesfully read mode1 frame #%d\n", pce_cd.current_frame );
		}

		pce_cd.data_buffer_index = 0;
		pce_cd.data_buffer_size = 2048;
		pce_cd.current_frame++;

		pce_cd.scsi_IO = 1;
		pce_cd.scsi_CD = 0;

		if ( pce_cd.current_frame == pce_cd.end_frame )
		{
			/* We are done, disable the timer */
			logerror("Last frame read from CD\n");
			pce_cd.data_transferred = 1;
			pce_cd.data_timer->adjust(attotime::never);
		}
		else
		{
			pce_cd.data_transferred = 0;
		}
	}
}

static void pce_cd_init( running_machine *machine )
{
	device_t *device;

	/* Initialize pce_cd struct */
	memset( &pce_cd, 0, sizeof(pce_cd) );

	/* Initialize BRAM */
	pce_cd.bram = auto_alloc_array(machine, UINT8, PCE_BRAM_SIZE * 2 );
	memset( pce_cd.bram, 0, PCE_BRAM_SIZE );
	memset( pce_cd.bram + PCE_BRAM_SIZE, 0xFF, PCE_BRAM_SIZE );
	pce_cd.bram_locked = 1;
	pce_set_cd_bram(machine);

	/* set up adpcm related things */
	pce_cd.adpcm_ram = auto_alloc_array(machine, UINT8, PCE_ADPCM_RAM_SIZE );
	memset( pce_cd.adpcm_ram, 0, PCE_ADPCM_RAM_SIZE );
	pce_cd.adpcm_clock_divider = 1;
	msm5205_change_clock_w(machine->device("msm5205"), (PCE_CD_CLOCK / 6) / pce_cd.adpcm_clock_divider);

	/* Set up cd command buffer */
	pce_cd.command_buffer = auto_alloc_array(machine, UINT8, PCE_CD_COMMAND_BUFFER_SIZE );
	memset( pce_cd.command_buffer, 0, PCE_CD_COMMAND_BUFFER_SIZE );
	pce_cd.command_buffer_index = 0;

	/* Set up Arcade Card RAM buffer */
	pce_cd.acard_ram = auto_alloc_array(machine, UINT8, PCE_ACARD_RAM_SIZE );
	memset( pce_cd.acard_ram, 0, PCE_ACARD_RAM_SIZE );

	pce_cd.data_buffer = auto_alloc_array(machine, UINT8, 8192 );
	memset( pce_cd.data_buffer, 0, 8192 );
	pce_cd.data_buffer_size = 0;
	pce_cd.data_buffer_index = 0;

	pce_cd.subcode_buffer = auto_alloc_array(machine, UINT8, 96 );

	device = machine->device("cdrom");
	if ( device )
	{
		pce_cd.cd = cd_get_cdrom_file(device);
		if ( pce_cd.cd )
		{
			pce_cd.toc = cdrom_get_toc( pce_cd.cd );
			cdda_set_cdrom( machine->device("cdda"), pce_cd.cd );
			pce_cd.last_frame = cdrom_get_track_start( pce_cd.cd, cdrom_get_last_track( pce_cd.cd ) - 1 );
			pce_cd.last_frame += pce_cd.toc->tracks[ cdrom_get_last_track( pce_cd.cd ) - 1 ].frames;
			pce_cd.end_frame = pce_cd.last_frame;
		}
	}

	pce_cd.data_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_data_timer_callback));
	pce_cd.data_timer->adjust(attotime::never);
	pce_cd.adpcm_dma_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_adpcm_dma_timer_callback));
	pce_cd.adpcm_dma_timer->adjust(attotime::never);

	pce_cd.cdda_fadeout_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_cdda_fadeout_callback));
	pce_cd.cdda_fadeout_timer->adjust(attotime::never);
	pce_cd.cdda_fadein_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_cdda_fadein_callback));
	pce_cd.cdda_fadein_timer->adjust(attotime::never);

	pce_cd.adpcm_fadeout_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_adpcm_fadeout_callback));
	pce_cd.adpcm_fadeout_timer->adjust(attotime::never);
	pce_cd.adpcm_fadein_timer = machine->scheduler().timer_alloc(FUNC(pce_cd_adpcm_fadein_callback));
	pce_cd.adpcm_fadein_timer->adjust(attotime::never);
}

WRITE8_HANDLER( pce_cd_bram_w )
{
	if ( ! pce_cd.bram_locked )
	{
		pce_cd.bram[ offset ] = data;
	}
}

static void pce_cd_set_adpcm_ram_byte(running_machine *machine, UINT8 val)
{
	if(pce_cd.adpcm_write_buf > 0)
	{
		pce_cd.adpcm_write_buf--;
	}
	else
	{
		pce_cd.adpcm_ram[pce_cd.adpcm_write_ptr] = val;
		pce_cd.adpcm_write_ptr = ((pce_cd.adpcm_write_ptr + 1) & 0xffff);
		//TODO: length + 1
	}
}

static TIMER_CALLBACK( pce_cd_cdda_fadeout_callback )
{
	pce_cd.cdda_volume-= 0.1;

	if(pce_cd.cdda_volume <= 0)
	{
		pce_cd.cdda_volume = 0.0;
		cdda_set_volume(machine->device("cdda"), 0.0);
		pce_cd.cdda_fadeout_timer->adjust(attotime::never);
	}
	else
	{
		cdda_set_volume(machine->device("cdda"), pce_cd.cdda_volume);
		pce_cd.cdda_fadeout_timer->adjust(attotime::from_usec(param), param);
	}
}

static TIMER_CALLBACK( pce_cd_cdda_fadein_callback )
{
	pce_cd.cdda_volume+= 0.1;

	if(pce_cd.cdda_volume >= 100.0)
	{
		pce_cd.cdda_volume = 100.0;
		cdda_set_volume(machine->device("cdda"), 100.0);
		pce_cd.cdda_fadein_timer->adjust(attotime::never);
	}
	else
	{
		cdda_set_volume(machine->device("cdda"), pce_cd.cdda_volume);
		pce_cd.cdda_fadein_timer->adjust(attotime::from_usec(param), param);
	}
}

static TIMER_CALLBACK( pce_cd_adpcm_fadeout_callback )
{
	pce_cd.adpcm_volume-= 0.1;

	if(pce_cd.adpcm_volume <= 0)
	{
		pce_cd.adpcm_volume = 0.0;
		msm5205_set_volume(machine->device("msm5205"), 0.0);
		pce_cd.adpcm_fadeout_timer->adjust(attotime::never);
	}
	else
	{
		msm5205_set_volume(machine->device("msm5205"), pce_cd.adpcm_volume);
		pce_cd.adpcm_fadeout_timer->adjust(attotime::from_usec(param), param);
	}
}

static TIMER_CALLBACK( pce_cd_adpcm_fadein_callback )
{
	pce_cd.adpcm_volume+= 0.1;

	if(pce_cd.adpcm_volume >= 100.0)
	{
		pce_cd.adpcm_volume = 100.0;
		msm5205_set_volume(machine->device("msm5205"), 100.0);
		pce_cd.adpcm_fadein_timer->adjust(attotime::never);
	}
	else
	{
		msm5205_set_volume(machine->device("msm5205"), pce_cd.adpcm_volume);
		pce_cd.adpcm_fadein_timer->adjust(attotime::from_usec(param), param);
	}
}


WRITE8_HANDLER( pce_cd_intf_w )
{
	pce_cd_update(space->machine);

	if(offset & 0x200 && pce_sys3_card && pce_acard) // route Arcade Card handling ports
		return pce_cd_acard_w(space,offset,data);

	logerror("%04X: write to CD interface offset %02X, data %02X\n", cpu_get_pc(space->cpu), offset, data );

	switch( offset & 0xf )
	{
	case 0x00:	/* CDC status */
		/* select device (which bits??) */
		pce_cd.scsi_SEL = 1;
		pce_cd_update(space->machine);
		pce_cd.scsi_SEL = 0;
		pce_cd.adpcm_dma_timer->adjust(attotime::never); // stop ADPCM DMA here
		/* any write here clears CD transfer irqs */
		pce_cd.regs[0x03] &= ~0x70;
		cputag_set_input_line(space->machine, "maincpu", 1, CLEAR_LINE );
		break;
	case 0x01:	/* CDC command / status / data */
		break;
	case 0x02:	/* ADPCM / CD control / IRQ enable/disable */
				/* bit 6 - transfer ready irq */
				/* bit 5 - transfer done irq */
				/* bit 4 - ?? irq */
				/* bit 3 - ?? irq */
				/* bit 2 - ?? irq */
		pce_cd.scsi_ACK = data & 0x80;
		/* Don't set or reset any irq lines, but just verify the current state */
		pce_cd_set_irq_line( space->machine, 0, 0 );
		break;
	case 0x03:	/* BRAM lock / CD status / IRQ - Read Only register */
		break;
	case 0x04:	/* CD reset */
		pce_cd.scsi_RST = data & 0x02;
		break;
	case 0x05:	/* Convert PCM data / PCM data */
	case 0x06:	/* PCM data */
		break;
	case 0x07:	/* BRAM unlock / CD status */
		if ( data & 0x80 )
		{
			pce_cd.bram_locked = 0;
			pce_set_cd_bram(space->machine);
		}
		break;
	case 0x08:	/* ADPCM address (LSB) / CD data */
		break;
	case 0x09:	/* ADPCM address (MSB) */
		break;
	case 0x0A:	/* ADPCM RAM data port */
		pce_cd_set_adpcm_ram_byte(space->machine, data);
		break;
	case 0x0B:	/* ADPCM DMA control */
		if ( data & 0x03 )
		{
			/* Start CD to ADPCM transfer */
			pce_cd.adpcm_dma_timer->adjust(attotime::from_hz( PCE_CD_DATA_FRAMES_PER_SECOND * 2048 ), 0, attotime::from_hz( PCE_CD_DATA_FRAMES_PER_SECOND * 2048 ) );
			pce_cd.regs[0x0c] |= 4;
		}
		break;
	case 0x0C:	/* ADPCM status */
		break;
	case 0x0D:	/* ADPCM address control */
		if ( ( pce_cd.regs[0x0D] & 0x80 ) && ! ( data & 0x80 ) ) // ADPCM reset
		{
			/* Reset ADPCM hardware */
			pce_cd.adpcm_read_ptr = 0;
			pce_cd.adpcm_write_ptr = 0;
			pce_cd.msm_start_addr = 0;
			pce_cd.msm_end_addr = 0;
			pce_cd.msm_half_addr = 0;
			pce_cd.msm_nibble = 0;
			adpcm_stop(space->machine);
			msm5205_reset_w( space->machine->device( "msm5205"), 1 );
		}

		if(data & 0x20)
			pce_cd.msm_half_addr = (pce_cd.adpcm_read_ptr + (pce_cd.adpcm_length / ((data & 0x40) ? 2 : 1))) & 0xffff;

		if ( ( data & 0x40) && ((pce_cd.regs[0x0D] & 0x40) == 0) ) // ADPCM play
		{
			pce_cd.msm_start_addr = (pce_cd.adpcm_read_ptr);
			pce_cd.msm_end_addr = (pce_cd.adpcm_read_ptr + pce_cd.adpcm_length) & 0xffff;
			pce_cd.msm_nibble = 0;
			adpcm_play(space->machine);
			msm5205_reset_w( space->machine->device( "msm5205"), 0 );

			//popmessage("%08x %08x",pce_cd.adpcm_read_ptr,pce_cd.adpcm_length);
		}
		else if ( (data & 0x40) == 0 )
		{
			/* used by Buster Bros to cancel an in-flight sample */
			adpcm_stop(space->machine);
			msm5205_reset_w( space->machine->device( "msm5205"), 1 );
		}

		if ( data & 0x10 ) //ADPCM set length
		{
			pce_cd.adpcm_length = ( pce_cd.regs[0x09] << 8 ) | pce_cd.regs[0x08];
		}
		if ( data & 0x08 ) //ADPCM set read address
		{
			pce_cd.adpcm_read_ptr = ( pce_cd.regs[0x09] << 8 ) | pce_cd.regs[0x08];
			pce_cd.adpcm_read_buf = 2;
		}
		if ( ( data & 0x02 ) == 0x02 ) //ADPCM set write address
		{
			pce_cd.adpcm_write_ptr = ( pce_cd.regs[0x09] << 8 ) | pce_cd.regs[0x08];
			pce_cd.adpcm_write_buf = data & 1;
		}
		break;
	case 0x0E:	/* ADPCM playback rate */
		pce_cd.adpcm_clock_divider = 0x10 - ( data & 0x0F );
		msm5205_change_clock_w(space->machine->device("msm5205"), (PCE_CD_CLOCK / 6) / pce_cd.adpcm_clock_divider);
		break;
	case 0x0F:	/* ADPCM and CD audio fade timer */
		/* TODO: timers needs HW tests */
		if(pce_cd.regs[0xf] != data)
		{
			switch(data & 0xf)
			{
				case 0x00: //CD-DA / ADPCM enable (100 msecs)
					pce_cd.cdda_volume = 0.0;
					pce_cd.cdda_fadein_timer->adjust(attotime::from_usec(100), 100);
					pce_cd.adpcm_volume = 0.0;
					pce_cd.adpcm_fadein_timer->adjust(attotime::from_usec(100), 100);
					pce_cd.cdda_fadeout_timer->adjust(attotime::never);
					pce_cd.adpcm_fadeout_timer->adjust(attotime::never);
					break;
				case 0x01: //CD-DA enable (100 msecs)
					pce_cd.cdda_volume = 0.0;
					pce_cd.cdda_fadein_timer->adjust(attotime::from_usec(100), 100);
					pce_cd.cdda_fadeout_timer->adjust(attotime::never);
					break;
				case 0x08: //CD-DA short (1500 msecs) fade out / ADPCM enable
					pce_cd.cdda_volume = 100.0;
					pce_cd.cdda_fadeout_timer->adjust(attotime::from_usec(1500), 1500);
					pce_cd.adpcm_volume = 0.0;
					pce_cd.adpcm_fadein_timer->adjust(attotime::from_usec(100), 100);
					pce_cd.cdda_fadein_timer->adjust(attotime::never);
					pce_cd.adpcm_fadeout_timer->adjust(attotime::never);
					break;
				case 0x09: //CD-DA long (5000 msecs) fade out
					pce_cd.cdda_volume = 100.0;
					pce_cd.cdda_fadeout_timer->adjust(attotime::from_usec(5000), 5000);
					pce_cd.cdda_fadein_timer->adjust(attotime::never);
					break;
				case 0x0a: //ADPCM long (5000 msecs) fade out
					pce_cd.adpcm_volume = 100.0;
					pce_cd.adpcm_fadeout_timer->adjust(attotime::from_usec(5000), 5000);
					pce_cd.adpcm_fadein_timer->adjust(attotime::never);
					break;
				case 0x0c: //CD-DA short (1500 msecs) fade out / ADPCM enable
					pce_cd.cdda_volume = 100.0;
					pce_cd.cdda_fadeout_timer->adjust(attotime::from_usec(1500), 1500);
					pce_cd.adpcm_volume = 0.0;
					pce_cd.adpcm_fadein_timer->adjust(attotime::from_usec(100), 100);
					pce_cd.cdda_fadein_timer->adjust(attotime::never);
					pce_cd.adpcm_fadeout_timer->adjust(attotime::never);
					break;
				case 0x0d: //CD-DA short (1500 msecs) fade out
					pce_cd.cdda_volume = 100.0;
					pce_cd.cdda_fadeout_timer->adjust(attotime::from_usec(1500), 1500);
					pce_cd.cdda_fadein_timer->adjust(attotime::never);
					break;
				case 0x0e: //ADPCM short (1500 msecs) fade out
					pce_cd.adpcm_volume = 100.0;
					pce_cd.adpcm_fadeout_timer->adjust(attotime::from_usec(1500), 1500);
					pce_cd.adpcm_fadein_timer->adjust(attotime::never);
					break;
				default:
					popmessage("CD-DA / ADPCM Fade effect mode %02x, contact MESSdev",data & 0x0f);
					break;
			}
		}
		break;
	default:
		return;
	}
	pce_cd.regs[offset & 0xf] = data;
	pce_cd_update(space->machine);
}

static TIMER_CALLBACK( pce_cd_clear_ack )
{
	pce_cd_update(machine);
	pce_cd.scsi_ACK = 0;
	pce_cd_update(machine);
	if ( pce_cd.scsi_CD )
	{
		pce_cd.regs[0x0B] &= 0xFE;
	}
}

static UINT8 pce_cd_get_cd_data_byte(running_machine *machine)
{
	UINT8 data = pce_cd.regs[0x01];
	if ( pce_cd.scsi_REQ && ! pce_cd.scsi_ACK && ! pce_cd.scsi_CD )
	{
		if ( pce_cd.scsi_IO )
		{
			pce_cd.scsi_ACK = 1;
			machine->scheduler().timer_set(machine->device<cpu_device>("maincpu")->cycles_to_attotime(15), FUNC(pce_cd_clear_ack));
		}
	}
	return data;
}


static TIMER_CALLBACK( pce_cd_adpcm_dma_timer_callback )
{
	if ( pce_cd.scsi_REQ && ! pce_cd.scsi_ACK && ! pce_cd.scsi_CD && pce_cd.scsi_IO  )
	{
		pce_cd.adpcm_ram[pce_cd.adpcm_write_ptr] = pce_cd_get_cd_data_byte(machine);
		pce_cd.adpcm_write_ptr = ( pce_cd.adpcm_write_ptr + 1 ) & 0xFFFF;

		pce_cd.regs[0x0c] &= ~4;
	}
}

static UINT8 pce_cd_get_adpcm_ram_byte(running_machine *machine)
{
	if(pce_cd.adpcm_read_buf > 0)
	{
		pce_cd.adpcm_read_buf--;
		return 0;
	}
	else
	{
		UINT8 res;

		res = pce_cd.adpcm_ram[pce_cd.adpcm_read_ptr];
		pce_cd.adpcm_read_ptr = ((pce_cd.adpcm_read_ptr + 1) & 0xffff);

		return res;
	}
}

READ8_HANDLER( pce_cd_intf_r )
{
	UINT8 data = pce_cd.regs[offset & 0x0F];

	pce_cd_update(space->machine);

	if(offset & 0x200 && pce_sys3_card && pce_acard) // route Arcade Card handling ports
		return pce_cd_acard_r(space,offset);

	logerror("%04X: read from CD interface offset %02X\n", cpu_get_pc(space->cpu), offset );

	if((offset & 0xc0) == 0xc0 && pce_sys3_card) //System 3 Card header handling
	{
		switch(offset & 0xcf)
		{
			case 0xc1: return 0xaa;
			case 0xc2: return 0x55;
			case 0xc3: return 0x00;
			case 0xc5: return (pce_sys3_card & 2) ? 0x55 : 0xaa;
			case 0xc6: return (pce_sys3_card & 2) ? 0xaa : 0x55;
			case 0xc7: return 0x03;
		}
	}

	switch( offset & 0xf )
	{
	case 0x00:	/* CDC status */
		data &= 0x07;
		data |= pce_cd.scsi_BSY ? 0x80 : 0;
		data |= pce_cd.scsi_REQ ? 0x40 : 0;
		data |= pce_cd.scsi_MSG ? 0x20 : 0;
		data |= pce_cd.scsi_CD  ? 0x10 : 0;
		data |= pce_cd.scsi_IO  ? 0x08 : 0;
		break;
	case 0x01:	/* CDC command / status / data */
		break;
	case 0x02:	/* ADPCM / CD control */
		break;
	case 0x03:	/* BRAM lock / CD status */
		/* bit 4 set when CD motor is on */
		/* bit 2 set when less than half of the ADPCM data is remaining ?? */
		pce_cd.bram_locked = 1;
		pce_set_cd_bram(space->machine);
		data = data & 0x6E;
		data |= ( pce_cd.cd_motor_on ? 0x10 : 0 );
		pce_cd.regs[0x03] ^= 0x02;			/* TODO: get rid of this hack */
		break;
	case 0x04:	/* CD reset */
		break;
	case 0x05:	/* Convert PCM data / PCM data */
	case 0x06:	/* PCM data */
		break;
	case 0x07:	/* BRAM unlock / CD status */
		data = ( pce_cd.bram_locked ? ( data & 0x7F ) : ( data | 0x80 ) );
		break;
	case 0x08:	/* ADPCM address (LSB) / CD data */
		data = pce_cd_get_cd_data_byte(space->machine);
		break;
	case 0x0A:	/* ADPCM RAM data port */
		data = pce_cd_get_adpcm_ram_byte(space->machine);
		break;
	case 0x0B:	/* ADPCM DMA control */
		break;
	case 0x0C:	/* ADPCM status */
		break;
	case 0x0D:	/* ADPCM address control */
		break;
	/* These are read-only registers */
	case 0x09:	/* ADPCM address (MSB) */
	case 0x0E:	/* ADPCM playback rate */
	case 0x0F:	/* ADPCM and CD audio fade timer */
		return 0;
	default:
		data = 0xFF;
		break;
	}

	return data;
}


/*

PC Engine Arcade Card emulation

*/

READ8_HANDLER( pce_cd_acard_r )
{
	UINT8 r_num;

	if((offset & 0x2e0) == 0x2e0)
	{
		switch(offset & 0x2ef)
		{
			case 0x2e0: return (pce_cd.acard_shift >> 0)  & 0xff;
			case 0x2e1: return (pce_cd.acard_shift >> 8)  & 0xff;
			case 0x2e2: return (pce_cd.acard_shift >> 16) & 0xff;
			case 0x2e3: return (pce_cd.acard_shift >> 24) & 0xff;
			case 0x2e4: return (pce_cd.acard_shift_reg);
			case 0x2e5: return pce_cd.acard_latch;
			case 0x2ee: return 0x10;
			case 0x2ef: return 0x51;
		}

		return 0;
	}

	r_num = (offset & 0x30) >> 4;

	switch(offset & 0x0f)
	{
		case 0x00:
		case 0x01:
		{
			UINT8 res;
			if(pce_cd.acard_ctrl[r_num] & 2)
				res = pce_cd.acard_ram[(pce_cd.acard_base_addr[r_num] + pce_cd.acard_addr_offset[r_num]) & 0x1fffff];
			else
				res = pce_cd.acard_ram[pce_cd.acard_base_addr[r_num] & 0x1fffff];

			if(pce_cd.acard_ctrl[r_num] & 0x1)
			{
				if(pce_cd.acard_ctrl[r_num] & 0x10)
				{
					pce_cd.acard_base_addr[r_num] += pce_cd.acard_addr_inc[r_num];
					pce_cd.acard_base_addr[r_num] &= 0xffffff;
				}
				else
				{
					pce_cd.acard_addr_offset[r_num] += pce_cd.acard_addr_inc[r_num];
				}
			}

			return res;
		}
		case 0x02: return (pce_cd.acard_base_addr[r_num] >> 0) & 0xff;
		case 0x03: return (pce_cd.acard_base_addr[r_num] >> 8) & 0xff;
		case 0x04: return (pce_cd.acard_base_addr[r_num] >> 16) & 0xff;
		case 0x05: return (pce_cd.acard_addr_offset[r_num] >> 0) & 0xff;
		case 0x06: return (pce_cd.acard_addr_offset[r_num] >> 8) & 0xff;
		case 0x07: return (pce_cd.acard_addr_inc[r_num] >> 0) & 0xff;
		case 0x08: return (pce_cd.acard_addr_inc[r_num] >> 8) & 0xff;
		case 0x09: return pce_cd.acard_ctrl[r_num];
		default:   return 0;
	}

	return 0;
}

WRITE8_HANDLER( pce_cd_acard_w )
{
	UINT8 w_num;

	if((offset & 0x2e0) == 0x2e0)
	{
		switch(offset & 0x0f)
		{
			case 0: pce_cd.acard_shift = (data & 0xff) | (pce_cd.acard_shift & 0xffffff00); break;
			case 1: pce_cd.acard_shift = (data << 8)   | (pce_cd.acard_shift & 0xffff00ff); break;
			case 2: pce_cd.acard_shift = (data << 16)  | (pce_cd.acard_shift & 0xff00ffff); break;
			case 3: pce_cd.acard_shift = (data << 24)  | (pce_cd.acard_shift & 0x00ffffff); break;
			case 4:
				{
					pce_cd.acard_shift_reg = data & 0x0f;

					if(pce_cd.acard_shift_reg != 0)
					{
						 pce_cd.acard_shift = (pce_cd.acard_shift_reg < 8) ?
											(pce_cd.acard_shift << pce_cd.acard_shift_reg)
											: (pce_cd.acard_shift >> (16 - pce_cd.acard_shift_reg));
					}
				}
				break;
			case 5: pce_cd.acard_latch = data; break;
		}
	}
	else
	{
		w_num = (offset & 0x30) >> 4;

		switch(offset & 0x0f)
		{
			case 0x00:
			case 0x01:
				if(pce_cd.acard_ctrl[w_num] & 2)
					pce_cd.acard_ram[(pce_cd.acard_base_addr[w_num] + pce_cd.acard_addr_offset[w_num]) & 0x1fffff] = data;
				else
					pce_cd.acard_ram[pce_cd.acard_base_addr[w_num] & 0x1FFFFF] = data;

				if(pce_cd.acard_ctrl[w_num] & 0x1)
				{
					if(pce_cd.acard_ctrl[w_num] & 0x10)
					{
						pce_cd.acard_base_addr[w_num] += pce_cd.acard_addr_inc[w_num];
						pce_cd.acard_base_addr[w_num] &= 0xffffff;
					}
					else
					{
						pce_cd.acard_addr_offset[w_num] += pce_cd.acard_addr_inc[w_num];
					}
				}

				break;

			case 0x02: pce_cd.acard_base_addr[w_num] = (data & 0xff) | (pce_cd.acard_base_addr[w_num] & 0xffff00);	break;
			case 0x03: pce_cd.acard_base_addr[w_num] = (data << 8) | (pce_cd.acard_base_addr[w_num] & 0xff00ff);		break;
			case 0x04: pce_cd.acard_base_addr[w_num] = (data << 16) | (pce_cd.acard_base_addr[w_num] & 0x00ffff);	break;
			case 0x05: pce_cd.acard_addr_offset[w_num] = (data & 0xff) | (pce_cd.acard_addr_offset[w_num] & 0xff00);	break;
			case 0x06:
				pce_cd.acard_addr_offset[w_num] = (data << 8) | (pce_cd.acard_addr_offset[w_num] & 0x00ff);

				if((pce_cd.acard_ctrl[w_num] & 0x60) == 0x40)
				{
					pce_cd.acard_base_addr[w_num] += pce_cd.acard_addr_offset[w_num] + ((pce_cd.acard_ctrl[w_num] & 0x08) ? 0xff0000 : 0);
					pce_cd.acard_base_addr[w_num] &= 0xffffff;
				}
				break;
			case 0x07: pce_cd.acard_addr_inc[w_num] = (data & 0xff) | (pce_cd.acard_addr_inc[w_num] & 0xff00);		break;
			case 0x08: pce_cd.acard_addr_inc[w_num] = (data << 8) | (pce_cd.acard_addr_inc[w_num] & 0x00ff);			break;
			case 0x09: pce_cd.acard_ctrl[w_num] = data & 0x7f;												break;
			case 0x0a:
				if((pce_cd.acard_ctrl[w_num] & 0x60) == 0x60)
				{
					pce_cd.acard_base_addr[w_num] += pce_cd.acard_addr_offset[w_num];
					pce_cd.acard_base_addr[w_num] &= 0xffffff;
				}
				break;
		}
	}
}

READ8_HANDLER( pce_cd_acard_wram_r )
{
	return pce_cd_intf_r(space,0x200 | (offset & 0x6000) >> 9);
}

WRITE8_HANDLER( pce_cd_acard_wram_w )
{
	pce_cd_intf_w(space,0x200 | (offset & 0x6000) >> 9,data);
}

