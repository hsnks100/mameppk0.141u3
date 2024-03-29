/*****************************************************************************
 *
 * includes/msx.h
 *
 ****************************************************************************/

#ifndef __MSX_H__
#define __MSX_H__

#include "machine/wd17xx.h"
#include "imagedev/flopimg.h"

#define MSX_MAX_CARTS	(2)

class msx_state : public driver_device
{
public:
	msx_state(running_machine &machine, const driver_device_config_base &config)
		: driver_device(machine, config) { }

	/* PSG */
	int psg_b;
	int opll_active;
	/* mouse */
	UINT16 mouse[2];
	int mouse_stat[2];
	/* rtc */
	int rtc_latch;
	/* disk */
	UINT8 dsk_stat;
	/* kanji */
	UINT8 *kanji_mem;
	int kanji_latch;
	/* memory */
	const msx_slot_layout *layout;
	slot_state *cart_state[MSX_MAX_CARTS];
	slot_state *state[4];
	const msx_slot *slot[4];
	UINT8 *ram_pages[4];
	UINT8 *empty, ram_mapper[4];
	UINT8 ramio_set_bits;
	slot_state *all_state[4][4][4];
	int slot_expanded[4];
	UINT8 primary_slot;
	UINT8 secondary_slot[4];
	UINT8 superloadrunner_bank;
	UINT8 korean90in1_bank;
	UINT8 *top_page;
	int port_c_old;
};


/*----------- defined in machine/msx.c -----------*/

extern const i8255a_interface msx_ppi8255_interface;
extern const wd17xx_interface msx_wd17xx_interface;
/* start/stop functions */
extern DRIVER_INIT( msx );
extern MACHINE_START( msx );
extern MACHINE_START( msx2 );
extern MACHINE_RESET( msx );
extern MACHINE_RESET( msx2 );
extern INTERRUPT_GEN( msx_interrupt );
extern INTERRUPT_GEN( msx2_interrupt );
extern NVRAM_HANDLER( msx2 );

DEVICE_IMAGE_LOAD( msx_cart );
DEVICE_IMAGE_UNLOAD( msx_cart );

void msx_vdp_interrupt(running_machine *machine, int i);

/* I/O functions */
READ8_DEVICE_HANDLER( msx_printer_status_r );
WRITE8_DEVICE_HANDLER( msx_printer_strobe_w );
WRITE8_DEVICE_HANDLER( msx_printer_data_w );

WRITE8_HANDLER ( msx_psg_port_a_w );
READ8_HANDLER ( msx_psg_port_a_r );
WRITE8_HANDLER ( msx_psg_port_b_w );
READ8_HANDLER ( msx_psg_port_b_r );
WRITE8_HANDLER ( msx_fmpac_w );
READ8_HANDLER ( msx_rtc_reg_r );
WRITE8_HANDLER ( msx_rtc_reg_w );
WRITE8_HANDLER ( msx_rtc_latch_w );
WRITE8_HANDLER ( msx_90in1_w );

/* new memory emulation */
WRITE8_HANDLER (msx_page0_w);
WRITE8_HANDLER (msx_page0_1_w);
WRITE8_HANDLER (msx_page1_w);
WRITE8_HANDLER (msx_page1_1_w);
WRITE8_HANDLER (msx_page1_2_w);
WRITE8_HANDLER (msx_page2_w);
WRITE8_HANDLER (msx_page2_1_w);
WRITE8_HANDLER (msx_page2_2_w);
WRITE8_HANDLER (msx_page2_3_w);
WRITE8_HANDLER (msx_page3_w);
WRITE8_HANDLER (msx_page3_1_w);
WRITE8_HANDLER (msx_sec_slot_w);
 READ8_HANDLER (msx_sec_slot_r);
WRITE8_HANDLER (msx_ram_mapper_w);
 READ8_HANDLER (msx_ram_mapper_r);
 READ8_HANDLER (msx_kanji_r);
WRITE8_HANDLER (msx_kanji_w);

#endif /* __MSX_H__ */
