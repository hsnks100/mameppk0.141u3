/*
 * msx_slot.c : definitions of the different slots
 *
 * Copyright (C) 2004 Sean Young
 *
 * Missing:
 * - Holy Qu'ran
 *   like ascii8, with switch address 5000h/5400h/5800h/5c00h, not working.
 * - Harry Fox
 *   16kb banks, 6000h and 7000h switch address; isn't it really an ascii16?
 * - Halnote
 *   writes to page 0?
 * - Playball
 *   Unemulated D7756C, same as src/drivers/homerun.c
 * - Some ascii8 w/ sram need 32kb sram?
 * - MegaRAM
 * - fmsx painter.rom
 */

#include "emu.h"
#include "emuopts.h"
#include "machine/i8255a.h"
#include "includes/msx_slot.h"
#include "includes/msx.h"
#include "machine/wd17xx.h"
#include "sound/k051649.h"
#include "sound/2413intf.h"
#include "sound/dac.h"
#include "sound/ay8910.h"

static void msx_cpu_setbank (running_machine *machine, int page, UINT8 *mem)
{
	msx_state *state = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	switch (page)
	{
	case 1:
		memory_set_bankptr (machine,"bank1", mem);
		break;
	case 2:
		memory_set_bankptr (machine,"bank2", mem);
		break;
	case 3:
		memory_set_bankptr (machine,"bank3", mem);
		break;
	case 4:
		memory_set_bankptr (machine,"bank4", mem);
		memory_set_bankptr (machine,"bank5", mem + 0x1ff8);
		memory_install_read_bank(space, 0x7ff8, 0x7fff, 0, 0, "bank5");
		break;
	case 5:
		memory_set_bankptr (machine,"bank6", mem);
		memory_set_bankptr (machine,"bank7", mem + 0x1800);
		memory_install_read_bank(space, 0x9800, 0x9fff, 0, 0, "bank7");
		break;
	case 6:
		memory_set_bankptr (machine,"bank8", mem);
		memory_set_bankptr (machine,"bank9", mem + 0x1800);
		memory_install_read_bank(space, 0xb800, 0xbfff, 0, 0, "bank9");
		break;
	case 7:
		memory_set_bankptr (machine,"bank10", mem);
		break;
	case 8:
		memory_set_bankptr (machine,"bank11", mem);
		state->top_page = mem;
		break;
	}
}

MSX_SLOT_INIT(empty)
{
	state->type = SLOT_EMPTY;

	return 0;
}

MSX_SLOT_RESET(empty)
{
	/* reset void */
}

MSX_SLOT_MAP(empty)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	msx_cpu_setbank (machine, page * 2 + 1, drvstate->empty);
	msx_cpu_setbank (machine, page * 2 + 2, drvstate->empty);
}

MSX_SLOT_INIT(rom)
{
	state->type = SLOT_ROM;
	state->mem = mem;
	state->size = size;
	state->start_page = page;

	return 0;
}

MSX_SLOT_RESET(rom)
{
	/* state-less */
}

MSX_SLOT_MAP(rom)
{
	UINT8 *mem = state->mem + (page - state->start_page) * 0x4000;

	msx_cpu_setbank (machine, page * 2 + 1, mem);
	msx_cpu_setbank (machine, page * 2 + 2, mem + 0x2000);
}

MSX_SLOT_INIT(ram)
{
	state->mem = auto_alloc_array(machine, UINT8, size);
	memset (state->mem, 0, size);
	state->type = SLOT_RAM;
	state->start_page = page;
	state->size = size;

	return 0;
}

MSX_SLOT_MAP(ram)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem = state->mem + (page - state->start_page) * 0x4000;

	drvstate->ram_pages[page] = mem;
	msx_cpu_setbank (machine, page * 2 + 1, mem);
	msx_cpu_setbank (machine, page * 2 + 2, mem + 0x2000);
}

MSX_SLOT_RESET(ram)
{
}

MSX_SLOT_INIT(rammm)
{
	int i, mask, nsize;
#ifdef MONMSX
	FILE *f;
#endif

	nsize = 0x10000; /* 64 kb */
	mask = 3;
	for (i=0; i<6; i++)
	{
		if (size == nsize)
		{
			break;
		}
		mask = (mask << 1) | 1;
		nsize <<= 1;
	}
	if (i == 6)
	{
		logerror ("ram mapper: error: must be 64kb, 128kb, 256kb, 512kb, "
				  "1mb, 2mb or 4mb\n");
		return 1;
	}
	state->mem = auto_alloc_array(machine, UINT8, size);
	memset (state->mem, 0, size);

#ifdef MONMSX
	f = fopen ("/home/sean/msx/hack/monmsx.bin", "r");
	if (f)
	{
		fseek (f, 6L, SEEK_SET);
		fread (state->mem, 1, 6151 - 6, f);
		fclose (f);
	}
#endif

	state->type = SLOT_RAM_MM;
	state->start_page = page;
	state->size = size;
	state->bank_mask = mask;

	return 0;
}

MSX_SLOT_RESET(rammm)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	int i;

	for (i=0; i<4; i++)
	{
		drvstate->ram_mapper[i] = 3 - i;
	}
}

MSX_SLOT_MAP(rammm)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem = state->mem +
			0x4000 * (drvstate->ram_mapper[page] & state->bank_mask);

	drvstate->ram_pages[page] = mem;
	msx_cpu_setbank (machine, page * 2 + 1, mem);
	msx_cpu_setbank (machine, page * 2 + 2, mem + 0x2000);
}

MSX_SLOT_INIT(msxdos2)
{
	if (size != 0x10000)
	{
		logerror ("msxdos2: error: rom file must be 64kb\n");
		return 1;
	}
	state->type = SLOT_MSXDOS2;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(msxdos2)
{
	state->banks[0] = 0;
}

MSX_SLOT_MAP(msxdos2)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (page != 1)
	{
		msx_cpu_setbank (machine, page * 2 + 1, drvstate->empty);
		msx_cpu_setbank (machine, page * 2 + 2, drvstate->empty);
	}
	else
	{
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x4000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[0] * 0x4000 + 0x2000);
	}
}

MSX_SLOT_WRITE(msxdos2)
{
	if (addr == 0x6000)
	{
		state->banks[0] = val & 3;
		slot_msxdos2_map (machine, state, 1);
	}
}

MSX_SLOT_INIT(konami)
{
	int banks;

	if (size > 0x200000)
	{
		logerror ("konami: warning: truncating to 2mb\n");
		size = 0x200000;
		return 1;
	}
	banks = size / 0x2000;
	if (size != banks * 0x2000 || (~(banks - 1) % banks))
	{
		logerror ("konami: error: must be a 2 power of 8kb\n");
		return 1;
	}
	state->type = SLOT_KONAMI;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(konami)
{
	int i;

	for (i=0; i<4; i++) state->banks[i] = i;
}

MSX_SLOT_MAP(konami)
{
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, state->mem);
		msx_cpu_setbank (machine, 2, state->mem + state->banks[1] * 0x2000);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 8, state->mem + state->banks[3] * 0x2000);
	}
}

MSX_SLOT_WRITE(konami)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	switch (addr)
	{
	case 0x6000:
		state->banks[1] = val & state->bank_mask;
		slot_konami_map (machine, state, 1);
		if (drvstate->state[0] == state)
		{
			slot_konami_map (machine, state, 0);
		}
		break;
	case 0x8000:
		state->banks[2] = val & state->bank_mask;
		slot_konami_map (machine, state, 2);
		if (drvstate->state[3] == state)
		{
			slot_konami_map (machine, state, 3);
		}
		break;
	case 0xa000:
		state->banks[3] = val & state->bank_mask;
		slot_konami_map (machine, state, 2);
		if (drvstate->state[3] == state)
		{
			slot_konami_map (machine, state, 3);
		}
	}
}

MSX_SLOT_INIT(konami_scc)
{
	int banks;

	if (size > 0x200000)
	{
		logerror ("konami_scc: warning: truncating to 2mb\n");
		size = 0x200000;
		return 1;
	}
	banks = size / 0x2000;
	if (size != banks * 0x2000 || (~(banks - 1) % banks))
	{
		logerror ("konami_scc: error: must be a 2 power of 8kb\n");
		return 1;
	}

	state->type = SLOT_KONAMI_SCC;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(konami_scc)
{
	int i;

	for (i=0; i<4; i++) state->banks[i] = i;
	state->cart.scc.active = 0;
}

static READ8_HANDLER (konami_scc_bank5)
{
	if (offset & 0x80)
	{
#if 0
		if ((offset & 0xff) >= 0xe0)
		{
			/* write 0xff to deformation register */
		}
#endif
		return 0xff;
	}
	else
	{
		return k051649_waveform_r (space->machine->device("k051649"), offset & 0x7f);
	}
}

MSX_SLOT_MAP(konami_scc)
{
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 2, state->mem + state->banks[3] * 0x2000);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		if (state->cart.scc.active ) {
			memory_install_read8_handler(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x9800, 0x9fff, 0, 0,konami_scc_bank5);
		} else {
			memory_install_read_bank(cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM), 0x9800, 0x9fff, 0, 0,"bank7");
		}
		break;
	case 3:
		msx_cpu_setbank (machine, 7, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 8, state->mem + state->banks[1] * 0x2000);
	}
}

MSX_SLOT_WRITE(konami_scc)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	if (addr >= 0x5000 && addr < 0x5800)
	{
		state->banks[0] = val & state->bank_mask;
		slot_konami_scc_map (machine, state, 1);
		if (drvstate->state[3] == state)
		{
			slot_konami_scc_map (machine, state, 3);
		}
	}
	else if (addr >= 0x7000 && addr < 0x7800)
	{
		state->banks[1] = val & state->bank_mask;
		slot_konami_scc_map (machine, state, 1);
		if (drvstate->state[3] == state)
		{
			slot_konami_scc_map (machine, state, 3);
		}
	}
	else if (addr >= 0x9000 && addr < 0x9800)
	{
		state->banks[2] = val & state->bank_mask;
		state->cart.scc.active = ((val & 0x3f) == 0x3f);
		slot_konami_scc_map (machine, state, 2);
		if (drvstate->state[0] == state)
		{
			slot_konami_scc_map (machine, state, 0);
		}
	}
	else if (state->cart.scc.active && addr >= 0x9800 && addr < 0xa000)
	{
		device_t *k051649 = space->machine->device("k051649");
		int offset = addr & 0xff;

		if (offset < 0x80)
		{
			k051649_waveform_w (k051649, offset, val);
		}
		else if (offset < 0xa0)
		{
			offset &= 0xf;
			if (offset < 0xa)
			{
				k051649_frequency_w (k051649, offset, val);
			}
			else if (offset < 0xf)
			{
				k051649_volume_w (k051649, offset - 0xa, val);
			}
			else
			{
				k051649_keyonoff_w (k051649, 0, val);
			}
		}
#if 0
		else if (offset >= 0xe0)
		{
			/* deformation register */
		}
#endif
	}
	else if (addr >= 0xb000 && addr < 0xb800)
	{
		state->banks[3] = val & state->bank_mask;
		slot_konami_scc_map (machine, state, 2);
		if (drvstate->state[0] == state)
		{
			slot_konami_scc_map (machine, state, 0);
		}
	}
}

MSX_SLOT_INIT(ascii8)
{
	int banks;

	if (size > 0x200000)
	{
		logerror ("ascii8: warning: truncating to 2mb\n");
		size = 0x200000;
		return 1;
	}
	banks = size / 0x2000;
	if (size != banks * 0x2000 || (~(banks - 1) % banks))
	{
		logerror ("ascii8: error: must be a 2 power of 8kb\n");
		return 1;
	}
	state->type = SLOT_ASCII8;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(ascii8)
{
	int i;

	for (i=0; i<4; i++) state->banks[i] = i;
}

MSX_SLOT_MAP(ascii8)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(ascii8)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	int bank;

	if (addr >= 0x6000 && addr < 0x8000)
	{
		bank = (addr / 0x800) & 3;

		state->banks[bank] = val & state->bank_mask;
		if (bank <= 1)
		{
			slot_ascii8_map (machine, state, 1);
		}
		else if (drvstate->state[2] == state)
		{
			slot_ascii8_map (machine, state, 2);
		}
	}
}

MSX_SLOT_INIT(ascii16)
{
	int banks;

	if (size > 0x400000)
	{
		logerror ("ascii16: warning: truncating to 4mb\n");
		size = 0x400000;
	}
	banks = size / 0x4000;
	if (size != banks * 0x4000 || (~(banks - 1) % banks))
	{
		logerror ("ascii16: error: must be a 2 power of 16kb\n");
		return 1;
	}

	state->type = SLOT_ASCII16;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(ascii16)
{
	int i;

	for (i=0; i<2; i++) state->banks[i] = i;
}

MSX_SLOT_MAP(ascii16)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;

	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		mem = state->mem + state->banks[0] * 0x4000;
		msx_cpu_setbank (machine, 3, mem);
		msx_cpu_setbank (machine, 4, mem + 0x2000);
		break;
	case 2:
		mem = state->mem + state->banks[1] * 0x4000;
		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(ascii16)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x6000 && addr < 0x6800)
	{
		state->banks[0] = val & state->bank_mask;
		slot_ascii16_map (machine, state, 1);
	}
	else if (addr >= 0x7000 && addr < 0x7800)
	{
		state->banks[1] = val & state->bank_mask;
		if (drvstate->state[2] == state)
		{
			slot_ascii16_map (machine, state, 2);
		}
	}
}

MSX_SLOT_INIT(ascii8_sram)
{
	static const char sramfile[] = "ascii8";
	int banks;

	state->cart.sram.mem = auto_alloc_array(machine, UINT8, 0x2000);
	if (size > 0x100000)
	{
		logerror ("ascii8_sram: warning: truncating to 1mb\n");
		size = 0x100000;
		return 1;
	}
	banks = size / 0x2000;
	if (size != banks * 0x2000 || (~(banks - 1) % banks))
	{
		logerror ("ascii8_sram: error: must be a 2 power of 8kb\n");
		return 1;
	}
	memset (state->cart.sram.mem, 0, 0x2000);
	state->type = SLOT_ASCII8_SRAM;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;
	state->cart.sram.sram_mask = banks;
	state->cart.sram.empty_mask = ~(banks | (banks - 1));
	if (!state->sramfile)
	{
		state->sramfile = sramfile;
	}

	return 0;
}

MSX_SLOT_RESET(ascii8_sram)
{
	int i;

	for (i=0; i<4; i++) state->banks[i] = i;
}

static UINT8 *ascii8_sram_bank_select (msx_state *drvstate, slot_state *state, int bankno)
{
	int bank = state->banks[bankno];

	if (bank & state->cart.sram.empty_mask)
	{
		return drvstate->empty;
	}
	else if (bank & state->cart.sram.sram_mask)
	{
		return state->cart.sram.mem;
	}
	else
	{
		return state->mem + (bank & state->bank_mask) * 0x2000;
	}
}

MSX_SLOT_MAP(ascii8_sram)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, ascii8_sram_bank_select (drvstate, state, 0));
		msx_cpu_setbank (machine, 4, ascii8_sram_bank_select (drvstate, state, 1));
		break;
	case 2:
		msx_cpu_setbank (machine, 5, ascii8_sram_bank_select (drvstate, state, 2));
		msx_cpu_setbank (machine, 6, ascii8_sram_bank_select (drvstate, state, 3));
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(ascii8_sram)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	int bank;

	if (addr >= 0x6000 && addr < 0x8000)
	{
		bank = (addr / 0x800) & 3;

		state->banks[bank] = val;
		if (bank <= 1)
		{
			slot_ascii8_sram_map (machine, state, 1);
		}
		else if (drvstate->state[2] == state)
		{
			slot_ascii8_sram_map (machine, state, 2);
		}
	}
	if (addr >= 0x8000 && addr < 0xc000)
	{
		bank = addr < 0xa000 ? 2 : 3;
		if (!(state->banks[bank] & state->cart.sram.empty_mask) &&
		     (state->banks[bank] & state->cart.sram.sram_mask))
		{
			state->cart.sram.mem[addr & 0x1fff] = val;
		}
	}
}

MSX_SLOT_LOADSRAM(ascii8_sram)
{
	if (!state->sramfile)
	{
		logerror ("ascii8_sram: error: no sram filename provided\n");
		return 1;
	}
	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_READ);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		if (f.read(state->cart.sram.mem, 0x2000) == 0x2000)
		{
			logerror ("ascii8_sram: info: sram loaded\n");
			return 0;
		}
		memset (state->cart.sram.mem, 0, 0x2000);
		logerror ("ascii8_sram: warning: could not read sram file\n");
		return 1;
	}

	logerror ("ascii8_sram: warning: could not open sram file for reading\n");

	return 1;
}

MSX_SLOT_SAVESRAM(ascii8_sram)
{
	if (!state->sramfile)
	{
		return 0;
	}

	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_WRITE);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		f.write(state->cart.sram.mem, 0x2000);
		logerror ("ascii8_sram: info: sram saved\n");

		return 0;
	}

	logerror ("ascii8_sram: warning: could not open sram file for saving\n");

	return 1;
}

MSX_SLOT_INIT(ascii16_sram)
{
	static const char sramfile[] = "ascii16";
	int banks;

	state->cart.sram.mem = auto_alloc_array(machine, UINT8, 0x4000);

	if (size > 0x200000)
	{
		logerror ("ascii16_sram: warning: truncating to 2mb\n");
		size = 0x200000;
	}
	banks = size / 0x4000;
	if (size != banks * 0x4000 || (~(banks - 1) % banks))
	{
		logerror ("ascii16_sram: error: must be a 2 power of 16kb\n");
		return 1;
	}

	memset (state->cart.sram.mem, 0, 0x4000);
	state->type = SLOT_ASCII16_SRAM;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;
	state->cart.sram.sram_mask = banks;
	state->cart.sram.empty_mask = ~(banks | (banks - 1));
	if (!state->sramfile)
	{
		state->sramfile = sramfile;
	}

	return 0;
}

MSX_SLOT_RESET(ascii16_sram)
{
	int i;

	for (i=0; i<2; i++) state->banks[i] = i;
}

static UINT8 *ascii16_sram_bank_select (msx_state *drvstate, slot_state *state, int bankno)
{
	int bank = state->banks[bankno];

	if (bank & state->cart.sram.empty_mask)
	{
		return drvstate->empty;
	}
	else if (bank & state->cart.sram.sram_mask)
	{
		return state->cart.sram.mem;
	}
	else
	{
		return state->mem + (bank & state->bank_mask) * 0x4000;
	}
}

MSX_SLOT_MAP(ascii16_sram)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;

	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		mem = ascii16_sram_bank_select (drvstate, state, 0);
		msx_cpu_setbank (machine, 3, mem);
		msx_cpu_setbank (machine, 4, mem + 0x2000);
		break;
	case 2:
		mem = ascii16_sram_bank_select (drvstate, state, 1);
		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(ascii16_sram)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x6000 && addr < 0x6800)
	{
		state->banks[0] = val;
		slot_ascii16_sram_map (machine, state, 1);
	}
	else if (addr >= 0x7000 && addr < 0x7800)
	{
		state->banks[1] = val;
		if (drvstate->state[2] == state)
		{
			slot_ascii16_sram_map (machine, state, 2);
		}
	}
	else if (addr >= 0x8000 && addr < 0xc000)
	{
		if (!(state->banks[1] & state->cart.sram.empty_mask) &&
		     (state->banks[1] & state->cart.sram.sram_mask))
		{
			int offset, i;

			offset = addr & 0x07ff;
			for (i=0; i<8; i++)
			{
				state->cart.sram.mem[offset] = val;
				offset += 0x0800;
			}
		}
	}
}

MSX_SLOT_LOADSRAM(ascii16_sram)
{
	UINT8 *p;

	if (!state->sramfile)
	{
		logerror ("ascii16_sram: error: no sram filename provided\n");
		return 1;
	}

	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_READ);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		p = state->cart.sram.mem;

		if (f.read(state->cart.sram.mem, 0x200) == 0x200)
		{
			int /*offset,*/ i;
			//offset = 0;
			for (i=0; i<7; i++)
			{
				memcpy (p + 0x800, p, 0x800);
				p += 0x800;
			}

			logerror ("ascii16_sram: info: sram loaded\n");
			return 0;
		}
		memset (state->cart.sram.mem, 0, 0x4000);
		logerror ("ascii16_sram: warning: could not read sram file\n");
		return 1;
	}

	logerror ("ascii16_sram: warning: could not open sram file for reading\n");

	return 1;
}

MSX_SLOT_SAVESRAM(ascii16_sram)
{
	if (!state->sramfile)
	{
		return 0;
	}
	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_WRITE);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		f.write(state->cart.sram.mem, 0x200);
		logerror ("ascii16_sram: info: sram saved\n");

		return 0;
	}

	logerror ("ascii16_sram: warning: could not open sram file for saving\n");

	return 1;
}

MSX_SLOT_INIT(rtype)
{
	if (!(size == 0x60000 || size == 0x80000))
	{
		logerror ("rtype: error: rom file should be exactly 384kb\n");
		return 1;
	}

	state->type = SLOT_RTYPE;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(rtype)
{
	state->banks[0] = 15;
}

MSX_SLOT_MAP(rtype)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;

	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		mem = state->mem + 15 * 0x4000;
		msx_cpu_setbank (machine, 3, mem);
		msx_cpu_setbank (machine, 4, mem + 0x2000);
		break;
	case 2:
		mem = state->mem + state->banks[0] * 0x4000;
		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(rtype)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x7000 && addr < 0x8000)
	{
		int data ;

		if (val & 0x10)
		{
			data = 0x10 | (val & 7);
		}
		else
		{
			data = val & 0x0f;
		}
		state->banks[0] = data;
		if (drvstate->state[2] == state)
		{
			slot_rtype_map (machine, state, 2);
		}
	}
}

MSX_SLOT_INIT(gmaster2)
{
	UINT8 *p;
	static const char sramfile[] = "GameMaster2";

	if (size != 0x20000)
	{
		logerror ("gmaster2: error: rom file should be 128kb\n");
		return 1;
	}
	state->type = SLOT_GAMEMASTER2;
	state->size = size;
	state->mem = mem;

	p = auto_alloc_array(machine, UINT8, 0x4000);
	memset (p, 0, 0x4000);
	state->cart.sram.mem = p;
	if (!state->sramfile)
	{
		state->sramfile = sramfile;
	}

	return 0;
}

MSX_SLOT_RESET(gmaster2)
{
	int i;

	for (i=0; i<4; i++)
	{
		state->banks[i] = i;
	}
}

MSX_SLOT_MAP(gmaster2)
{
	switch (page)
	{
	case 0:
	case 1:
		msx_cpu_setbank (machine, 1 + page * 2, state->mem); /* bank 0 is hardwired */
		if (state->banks[1] > 15)
		{
			msx_cpu_setbank (machine, 2 + page * 2, state->cart.sram.mem +
					(state->banks[1] - 16) * 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 2 + page * 2, state->mem + state->banks[1] * 0x2000);
		}
		break;
	case 2:
	case 3:
		if (state->banks[2] > 15)
		{
			msx_cpu_setbank (machine, 5 + page * 2, state->cart.sram.mem +
					(state->banks[2] - 16) * 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 5 + page * 2, state->mem + state->banks[2] * 0x2000);
		}
		if (state->banks[3] > 15)
		{
			msx_cpu_setbank (machine, 6 + page * 2, state->cart.sram.mem +
					(state->banks[3] - 16) * 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 6 + page * 2, state->mem + state->banks[3] * 0x2000);
		}
		break;
	}
}

MSX_SLOT_WRITE(gmaster2)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x6000 && addr < 0x7000)
	{
		if (val & 0x10)
		{
			val = val & 0x20 ? 17 : 16;
		}
		else
		{
			val = val & 15;
		}
		state->banks[1] = val;
		slot_gmaster2_map (machine, state, 1);
		if (drvstate->state[0] == state)
		{
			slot_gmaster2_map (machine, state, 0);
		}
	}
	else if (addr >= 0x8000 && addr < 0x9000)
	{
		if (val & 0x10)
		{
			val = val & 0x20 ? 17 : 16;
		}
		else
		{
			val = val & 15;
		}
		state->banks[2] = val;
		slot_gmaster2_map (machine, state, 2);
		if (drvstate->state[3] == state)
		{
			slot_gmaster2_map (machine, state, 3);
		}
	}
	else if (addr >= 0xa000 && addr < 0xb000)
	{
		if (val & 0x10)
		{
			val = val & 0x20 ? 17 : 16;
		}
		else
		{
			val = val & 15;
		}
		state->banks[3] = val;
		slot_gmaster2_map (machine, state, 2);
		if (drvstate->state[3] == state)
		{
			slot_gmaster2_map (machine, state, 3);
		}
	}
	else if (addr >= 0xb000 && addr < 0xc000)
	{
		addr &= 0x0fff;
		switch (state->banks[3])
		{
		case 16:
			state->cart.sram.mem[addr] = val;
			state->cart.sram.mem[addr + 0x1000] = val;
			break;
		case 17:
			state->cart.sram.mem[addr + 0x2000] = val;
			state->cart.sram.mem[addr + 0x3000] = val;
			break;
		}
	}
}

MSX_SLOT_LOADSRAM(gmaster2)
{
	UINT8 *p;

	p = state->cart.sram.mem;
	
	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_READ);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		if (f.read(p + 0x1000, 0x2000) == 0x2000)
		{
			memcpy (p, p + 0x1000, 0x1000);
			memcpy (p + 0x3000, p + 0x2000, 0x1000);
			logerror ("gmaster2: info: sram loaded\n");
			return 0;
		}
		memset (p, 0, 0x4000);
		logerror ("gmaster2: warning: could not read sram file\n");
		return 1;
	}

	logerror ("gmaster2: warning: could not open sram file for reading\n");

	return 1;
}

MSX_SLOT_SAVESRAM(gmaster2)
{
	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_WRITE);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		f.write(state->cart.sram.mem + 0x1000, 0x2000);
		logerror ("gmaster2: info: sram saved\n");

		return 0;
	}

	logerror ("gmaster2: warning: could not open sram file for saving\n");

	return 1;
}

MSX_SLOT_INIT(diskrom)
{
	if (size != 0x4000)
	{
		logerror ("diskrom: error: the diskrom should be 16kb\n");
		return 1;
	}

	state->type = SLOT_DISK_ROM;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(diskrom)
{
	device_t *fdc = machine->device("wd179x");
	wd17xx_reset(fdc);
}

static READ8_HANDLER (msx_diskrom_page1_r)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	device_t *fdc = space->machine->device("wd179x");
	switch (offset)
	{
	case 0: return wd17xx_status_r (fdc, 0);
	case 1: return wd17xx_track_r (fdc, 0);
	case 2: return wd17xx_sector_r (fdc, 0);
	case 3: return wd17xx_data_r (fdc, 0);
	case 7: return state->dsk_stat;
	default:
		return state->state[1]->mem[offset + 0x3ff8];
	}
}

static READ8_HANDLER (msx_diskrom_page2_r)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	device_t *fdc = space->machine->device("wd179x");
	if (offset >= 0x7f8)
	{
		switch (offset)
		{
		case 0x7f8:
			return wd17xx_status_r (fdc, 0);
		case 0x7f9:
			return wd17xx_track_r (fdc, 0);
		case 0x7fa:
			return wd17xx_sector_r (fdc, 0);
		case 0x7fb:
			return wd17xx_data_r (fdc, 0);
		case 0x7ff:
			return state->dsk_stat;
		default:
			return state->state[2]->mem[offset + 0x3800];
		}
	}
	else
	{
		return 0xff;
	}
}

MSX_SLOT_MAP(diskrom)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem);
		msx_cpu_setbank (machine, 4, state->mem + 0x2000);
		memory_install_read8_handler(space, 0x7ff8, 0x7fff, 0, 0, msx_diskrom_page1_r);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, drvstate->empty);
		msx_cpu_setbank (machine, 6, drvstate->empty);
		memory_install_read8_handler(space, 0xb800, 0xbfff, 0, 0, msx_diskrom_page2_r);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
		break;
	}
}

MSX_SLOT_WRITE(diskrom)
{
	device_t *fdc = machine->device("wd179x");
	if (addr >= 0xa000 && addr < 0xc000)
	{
		addr -= 0x4000;
	}
	switch (addr)
	{
	case 0x7ff8:
		wd17xx_command_w (fdc, 0, val);
		break;
	case 0x7ff9:
		wd17xx_track_w (fdc, 0, val);
		break;
	case 0x7ffa:
		wd17xx_sector_w (fdc, 0, val);
		break;
	case 0x7ffb:
		wd17xx_data_w (fdc, 0, val);
		break;
	case 0x7ffc:
		wd17xx_set_side (fdc,val & 1);
		state->mem[0x3ffc] = val | 0xfe;
		break;
	case 0x7ffd:
		wd17xx_set_drive (fdc,val & 1);
		if ((state->mem[0x3ffd] ^ val) & 0x40)
		{
			set_led_status (machine, 0, !(val & 0x40));
		}
		state->mem[0x3ffd] = (val | 0x7c) & ~0x04;
		break;
	}
}

MSX_SLOT_INIT(diskrom2)
{
	if (size != 0x4000)
	{
		logerror ("diskrom2: error: the diskrom2 should be 16kb\n");
		return 1;
	}

	state->type = SLOT_DISK_ROM2;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(diskrom2)
{
	device_t *fdc = machine->device("wd179x");
	wd17xx_reset (fdc);
}

static READ8_HANDLER (msx_diskrom2_page1_r)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	device_t *fdc = space->machine->device("wd179x");
	switch (offset)
	{
	case 0: return wd17xx_status_r(fdc, 0);
	case 1: return wd17xx_track_r(fdc, 0);
	case 2: return wd17xx_sector_r(fdc, 0);
	case 3: return wd17xx_data_r(fdc, 0);
	case 4: return state->dsk_stat;
	default:
		return state->state[1]->mem[offset + 0x3ff8];
	}
}

static  READ8_HANDLER (msx_diskrom2_page2_r)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	device_t *fdc = space->machine->device("wd179x");
	if (offset >= 0x7b8)
	{
		switch (offset)
		{
		case 0x7b8:
			return wd17xx_status_r (fdc, 0);
		case 0x7b9:
			return wd17xx_track_r (fdc, 0);
		case 0x7ba:
			return wd17xx_sector_r (fdc, 0);
		case 0x7bb:
			return wd17xx_data_r (fdc, 0);
		case 0x7bc:
			return state->dsk_stat;
		default:
			return state->state[2]->mem[offset + 0x3800];
		}
	}
	else
	{
		return 0xff;
	}
}

MSX_SLOT_MAP(diskrom2)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem);
		msx_cpu_setbank (machine, 4, state->mem + 0x2000);
		memory_install_read8_handler(space, 0x7fb8, 0x7fbc, 0, 0, msx_diskrom2_page1_r);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, drvstate->empty);
		msx_cpu_setbank (machine, 6, drvstate->empty);
		memory_install_read8_handler(space, 0xb800, 0xbfbc, 0, 0, msx_diskrom2_page2_r);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(diskrom2)
{
	device_t *fdc = machine->device("wd179x");
	if (addr >= 0xa000 && addr < 0xc000)
	{
		addr -= 0x4000;
	}
	switch (addr)
	{
	case 0x7fb8:
		wd17xx_command_w (fdc, 0, val);
		break;
	case 0x7fb9:
		wd17xx_track_w (fdc, 0, val);
		break;
	case 0x7fba:
		wd17xx_sector_w (fdc, 0, val);
		break;
	case 0x7fbb:
		wd17xx_data_w (fdc, 0, val);
		break;
	case 0x7fbc:
		wd17xx_set_side (fdc,val & 1);
		state->mem[0x3fbc] = val | 0xfe;
		wd17xx_set_drive (fdc,val & 1);
		if ((state->mem[0x3fbc] ^ val) & 0x40)
		{
			set_led_status (machine, 0, !(val & 0x40));
		}
		state->mem[0x3fbc] = (val | 0x7c) & ~0x04;
		break;
	}
}

MSX_SLOT_INIT(synthesizer)
{
	if (size != 0x8000)
	{
		logerror ("synthesizer: error: rom file must be 32kb\n");
		return 1;
	}
	state->type = SLOT_SYNTHESIZER;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(synthesizer)
{
	/* empty */
}

MSX_SLOT_MAP(synthesizer)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem);
		msx_cpu_setbank (machine, 4, state->mem + 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + 0x4000);
		msx_cpu_setbank (machine, 6, state->mem + 0x6000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(synthesizer)
{
	if (addr >= 0x4000 && addr < 0x8000 && !(addr & 0x0010))
	{
		dac_data_w (machine->device("dac"), val);
	}
}

MSX_SLOT_INIT(majutsushi)
{
	if (size != 0x20000)
	{
		logerror ("majutsushi: error: rom file must be 128kb\n");
		return 1;
	}
	state->type = SLOT_MAJUTSUSHI;
	state->mem = mem;
	state->size = size;
	state->bank_mask = 0x0f;

	return 0;
}

MSX_SLOT_RESET(majutsushi)
{
	int i;

	for (i=0; i<4; i++)
	{
		state->banks[i] = i;
	}
}

MSX_SLOT_MAP(majutsushi)
{
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 2, state->mem + state->banks[1] * 0x2000);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 8, state->mem + state->banks[3] * 0x2000);
		break;
	}
}

MSX_SLOT_WRITE(majutsushi)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x5000 && addr < 0x6000)
	{
		dac_data_w (machine->device("dac"), val);
	}
	else if (addr >= 0x6000 && addr < 0x8000)
	{
		state->banks[1] = val & 0x0f;
		slot_majutsushi_map (machine, state, 1);
		if (drvstate->state[0] == state)
		{
			slot_konami_map (machine, state, 0);
		}
	}
	else if (addr >= 0x8000 && addr < 0xc000)
	{
		state->banks[addr < 0xa000 ? 2 : 3] = val & 0x0f;
		slot_majutsushi_map (machine, state, 2);
		if (drvstate->state[3] == state)
		{
			slot_konami_map (machine, state, 3);
		}
	}
}

MSX_SLOT_INIT(fmpac)
{
	static const char sramfile[] = "fmpac.rom";
	UINT8 *p;
	int banks;

	if (size > 0x400000)
	{
		logerror ("fmpac: warning: truncating rom to 4mb\n");
		size = 0x400000;
	}
	banks = size / 0x4000;
	if (size != banks * 0x4000 || (~(banks - 1) % banks))
	{
		logerror ("fmpac: error: must be a 2 power of 16kb\n");
		return 1;
	}

	if (!strncmp ((char*)mem + 0x18, "PAC2", 4))
	{
		state->cart.fmpac.sram_support = 1;
		p = auto_alloc_array(machine, UINT8, 0x4000);
		memset (p, 0, 0x2000);
		memset (p + 0x2000, 0xff, 0x2000);
		state->cart.fmpac.mem = p;
	}
	else
	{
		state->cart.fmpac.sram_support = 0;
		state->cart.fmpac.mem = NULL;
	}

	state->type = SLOT_FMPAC;
	state->size = size;
	state->mem = mem;
	state->bank_mask = banks - 1;
	if (!state->sramfile)
	{
		state->sramfile = sramfile;
	}

	return 0;
}

MSX_SLOT_RESET(fmpac)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	int i;

	state->banks[0] = 0;
	state->cart.fmpac.sram_active = 0;
	state->cart.fmpac.opll_active = 0;
	drvstate->opll_active = 0;
	for (i=0; i<=state->bank_mask; i++)
	{
		state->mem[0x3ff6 + i * 0x4000] = 0;
	}

	/* NPW 21-Feb-2004 - Adding check for null */
	if (state->cart.fmpac.mem)
	{
		state->cart.fmpac.mem[0x3ff6] = 0;
		state->cart.fmpac.mem[0x3ff7] = 0;
	}

	/* IMPROVE: reset sound chip */
}

MSX_SLOT_MAP(fmpac)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (page == 1)
	{
		if (state->cart.fmpac.sram_active)
		{
			msx_cpu_setbank (machine, 3, state->cart.fmpac.mem);
			msx_cpu_setbank (machine, 4, state->cart.fmpac.mem + 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x4000);
			msx_cpu_setbank (machine, 4, state->mem + state->banks[0] * 0x4000 + 0x2000);
		}
	}
	else
	{
		msx_cpu_setbank (machine, page * 2 + 1, drvstate->empty);
		msx_cpu_setbank (machine, page * 2 + 2, drvstate->empty);
	}
}

MSX_SLOT_WRITE(fmpac)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	int i, data;

	if (addr >= 0x4000 && addr < 0x6000 && state->cart.fmpac.sram_support)
	{
		if (state->cart.fmpac.sram_active || addr >= 0x5ffe)
		{
			state->cart.fmpac.mem[addr & 0x1fff] = val;
		}

		state->cart.fmpac.sram_active =
				(state->cart.fmpac.mem[0x1ffe] == 0x4d &&
				 state->cart.fmpac.mem[0x1fff] == 0x69);
	}

	switch (addr)
	{
	case 0x7ff4:
		if (state->cart.fmpac.opll_active)
		{
			ym2413_w (space->machine->device("ay8910"), 0, val);
		}
		break;
	case 0x7ff5:
		if (state->cart.fmpac.opll_active)
		{
			ym2413_w (space->machine->device("ay8910"), 1, val);
		}
		break;
	case 0x7ff6:
		data = val & 0x11;
		for (i=0; i<=state->bank_mask; i++)
		{
			state->mem[0x3ff6 + i * 0x4000] = data;
		}
		state->cart.fmpac.mem[0x3ff6] = data;
		state->cart.fmpac.opll_active = val & 1;
		if ((drvstate->opll_active ^ val) & 1)
		{
			logerror ("FM-PAC: OPLL %sactivated\n", val & 1 ? "" : "de");
		}
		drvstate->opll_active = val & 1;
		break;
	case 0x7ff7:
		state->banks[0] = val & state->bank_mask;
		state->cart.fmpac.mem[0x3ff7] = val & state->bank_mask;
		slot_fmpac_map (machine, state, 1);
		break;
	}
}

static const char PAC_HEADER[] = "PAC2 BACKUP DATA";
#define PAC_HEADER_LEN (16)

MSX_SLOT_LOADSRAM(fmpac)
{
	char buf[PAC_HEADER_LEN];

	if (!state->cart.fmpac.sram_support)
	{
		logerror ("Your fmpac.rom does not support sram\n");
		return 1;
	}

	if (!state->sramfile)
	{
		logerror ("No sram filename provided\n");
		return 1;
	}
	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_READ);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		if ((f.read(buf, PAC_HEADER_LEN) == PAC_HEADER_LEN) &&
			!strncmp (buf, PAC_HEADER, PAC_HEADER_LEN) &&
			f.read(state->cart.fmpac.mem, 0x1ffe))
		{
			logerror ("fmpac: info: sram loaded\n");
			return 0;
		}
		else
		{
			logerror ("fmpac: warning: failed to load sram\n");
			return 1;
		}
	}

	logerror ("fmpac: warning: could not open sram file\n");
	return 1;
}

MSX_SLOT_SAVESRAM(fmpac)
{
	if (!state->cart.fmpac.sram_support || !state->sramfile)
	{
		return 0;
	}

	emu_file f(machine->options(), SEARCHPATH_MEMCARD, OPEN_FLAG_WRITE);
	file_error filerr = f.open(state->sramfile);
	if (filerr == FILERR_NONE)
	{
		if ((f.write(PAC_HEADER, PAC_HEADER_LEN) == PAC_HEADER_LEN) &&
			(f.write(state->cart.fmpac.mem, 0x1ffe) == 0x1ffe))
		{
			logerror ("fmpac: info: sram saved\n");
			return 0;
		}
		else
		{
			logerror ("fmpac: warning: sram save to file failed\n");
			return 1;
		}
	}

	logerror ("fmpac: warning: could not open sram file for writing\n");

	return 1;
}

MSX_SLOT_INIT(superloadrunner)
{
	if (size != 0x20000)
	{
		logerror ("superloadrunner: error: rom file should be exactly "
				  "128kb\n");
		return 1;
	}
	state->type = SLOT_SUPERLOADRUNNER;
	state->mem = mem;
	state->size = size;
	state->start_page = page;
	state->bank_mask = 7;

	return 0;
}

MSX_SLOT_RESET(superloadrunner)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	drvstate->superloadrunner_bank = 0;
}

MSX_SLOT_MAP(superloadrunner)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (page == 2)
	{
		UINT8 *mem = state->mem +
				(drvstate->superloadrunner_bank & state->bank_mask) * 0x4000;

		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
	}
	else
	{
		msx_cpu_setbank (machine, page * 2 + 1, drvstate->empty);
		msx_cpu_setbank (machine, page * 2 + 2, drvstate->empty);
	}
}

MSX_SLOT_INIT(crossblaim)
{
	if (size != 0x10000)
	{
		logerror ("crossblaim: error: rom file should be exactly 64kb\n");
		return 1;
	}
	state->type = SLOT_CROSS_BLAIM;
	state->mem = mem;
	state->size = size;

	return 0;
}

MSX_SLOT_RESET(crossblaim)
{
	state->banks[0] = 1;
}

MSX_SLOT_MAP(crossblaim)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;

	/* This might look odd, but it's what happens on the real cartridge */

	switch (page)
	{
	case 0:
		if (state->banks[0] < 2){
			mem = state->mem + state->banks[0] * 0x4000;
			msx_cpu_setbank (machine, 1, mem);
			msx_cpu_setbank (machine, 2, mem + 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 1, drvstate->empty);
			msx_cpu_setbank (machine, 2, drvstate->empty);
		}
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem);
		msx_cpu_setbank (machine, 4, state->mem + 0x2000);
		break;
	case 2:
		mem = state->mem + state->banks[0] * 0x4000;
		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
		break;
	case 3:
		if (state->banks[0] < 2){
			mem = state->mem + state->banks[0] * 0x4000;
			msx_cpu_setbank (machine, 7, mem);
			msx_cpu_setbank (machine, 8, mem + 0x2000);
		}
		else
		{
			msx_cpu_setbank (machine, 7, drvstate->empty);
			msx_cpu_setbank (machine, 8, drvstate->empty);
		}
	}
}

MSX_SLOT_WRITE(crossblaim)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 block = val & 3;

	if (!block) block = 1;
	state->banks[0] = block;

	if (drvstate->state[0] == state)
	{
		slot_crossblaim_map (machine, state, 0);
	}
	if (drvstate->state[2] == state)
	{
		slot_crossblaim_map (machine, state, 2);
	}
	if (drvstate->state[3] == state)
	{
		slot_crossblaim_map (machine, state, 3);
	}
}

MSX_SLOT_INIT(korean80in1)
{
	int banks;

	if (size > 0x200000)
	{
		logerror ("korean-80in1: warning: truncating to 2mb\n");
		size = 0x200000;
	}
	banks = size / 0x2000;
	if (size != banks * 0x2000 || (~(banks - 1) % banks))
	{
		logerror ("korean-80in1: error: must be a 2 power of 8kb\n");
		return 1;
	}
	state->type = SLOT_KOREAN_80IN1;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(korean80in1)
{
	int i;

	for (i=0; i<4; i++)
	{
		state->banks[i] = i;
	}
}

MSX_SLOT_MAP(korean80in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(korean80in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	int bank;

	if (addr >= 0x4000 && addr < 0x4004)
	{
		bank = addr & 3;

		state->banks[bank] = val & state->bank_mask;
		if (bank <= 1)
		{
			slot_korean80in1_map (machine, state, 1);
		}
		else if (drvstate->state[2] == state)
		{
			slot_korean80in1_map (machine, state, 2);
		}
	}
}

MSX_SLOT_INIT(korean90in1)
{
	int banks;

	if (size > 0x100000)
	{
		logerror ("korean-90in1: warning: truncating to 1mb\n");
		size = 0x100000;
	}
	banks = size / 0x4000;
	if (size != banks * 0x4000 || (~(banks - 1) % banks))
	{
		logerror ("korean-90in1: error: must be a 2 power of 16kb\n");
		return 1;
	}
	state->type = SLOT_KOREAN_90IN1;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(korean90in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	drvstate->korean90in1_bank = 0;
}

MSX_SLOT_MAP(korean90in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;
	UINT8 mask = (drvstate->korean90in1_bank & 0xc0) == 0x80 ? 0x3e : 0x3f;
	mem = state->mem +
		((drvstate->korean90in1_bank & mask) & state->bank_mask) * 0x4000;

	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, mem);
		msx_cpu_setbank (machine, 4, mem + 0x2000);
		break;
	case 2:
		switch (drvstate->korean90in1_bank & 0xc0)
		{
		case 0x80: /* 32 kb mode */
			mem += 0x4000;
		default: /* ie. 0x00 and 0x40: same memory as page 1 */
			msx_cpu_setbank (machine, 5, mem);
			msx_cpu_setbank (machine, 6, mem + 0x2000);
			break;
		case 0xc0: /* same memory as page 1, but swap lower/upper 8kb */
			msx_cpu_setbank (machine, 5, mem + 0x2000);
			msx_cpu_setbank (machine, 6, mem);
			break;
		}
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_INIT(korean126in1)
{
	int banks;

	if (size > 0x400000)
	{
		logerror ("korean-126in1: warning: truncating to 4mb\n");
		size = 0x400000;
	}
	banks = size / 0x4000;
	if (size != banks * 0x4000 || (~(banks - 1) % banks))
	{
		logerror ("korean-126in1: error: must be a 2 power of 16kb\n");
		return 1;
	}

	state->type = SLOT_KOREAN_126IN1;
	state->mem = mem;
	state->size = size;
	state->bank_mask = banks - 1;

	return 0;
}

MSX_SLOT_RESET(korean126in1)
{
	int i;

	for (i=0; i<2; i++) state->banks[i] = i;
}

MSX_SLOT_MAP(korean126in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	UINT8 *mem;

	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, drvstate->empty);
		msx_cpu_setbank (machine, 2, drvstate->empty);
		break;
	case 1:
		mem = state->mem + state->banks[0] * 0x4000;
		msx_cpu_setbank (machine, 3, mem);
		msx_cpu_setbank (machine, 4, mem + 0x2000);
		break;
	case 2:
		mem = state->mem + state->banks[1] * 0x4000;
		msx_cpu_setbank (machine, 5, mem);
		msx_cpu_setbank (machine, 6, mem + 0x2000);
		break;
	case 3:
		msx_cpu_setbank (machine, 7, drvstate->empty);
		msx_cpu_setbank (machine, 8, drvstate->empty);
	}
}

MSX_SLOT_WRITE(korean126in1)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	if (addr >= 0x4000 && addr < 0x4002)
	{
		int bank = addr & 1;
		state->banks[bank] = val & state->bank_mask;
		if (bank == 0)
		{
			slot_korean126in1_map (machine, state, 1);
		}
		else if (drvstate->state[2] == state)
		{
			slot_korean126in1_map (machine, state, 2);
		}
	}
}

MSX_SLOT_INIT(soundcartridge)
{
	UINT8 *p;

	p = auto_alloc_array(machine, UINT8, 0x20000);
	memset (p, 0, 0x20000);

	state->mem = p;
	state->size = 0x20000;
	state->bank_mask = 15;
	state->type = SLOT_SOUNDCARTRIDGE;

	return 0;
}

MSX_SLOT_RESET(soundcartridge)
{
	int i;

	for (i=0; i<4; i++)
	{
		state->banks[i] = i;
		state->cart.sccp.ram_mode[i] = 0;
		state->cart.sccp.banks_saved[i] = i;
	}
	state->cart.sccp.mode = 0;
	state->cart.sccp.scc_active = 0;
	state->cart.sccp.sccp_active = 0;
}

static  READ8_HANDLER (soundcartridge_scc)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	int reg;


	if (offset >= 0x7e0)
	{
		return state->state[2]->mem[
				state->state[2]->banks[2] * 0x2000 + 0x1800 + offset];
	}

	reg = offset & 0xff;

	if (reg < 0x80)
	{
		return k051649_waveform_r (space->machine->device("k051649"), reg);
	}
	else if (reg < 0xa0)
	{
		/* nothing */
	}
	else if (reg < 0xc0)
	{
		/* read wave 5 */
		return k051649_waveform_r (space->machine->device("k051649"), 0x80 + (reg & 0x1f));
	}
#if 0
	else if (reg < 0xe0)
	{
		/* write 0xff to deformation register */
	}
#endif

	return 0xff;
}

static  READ8_HANDLER (soundcartridge_sccp)
{
	msx_state *state = space->machine->driver_data<msx_state>();
	int reg;

	if (offset >= 0x7e0)
	{
		return state->state[2]->mem[
				state->state[2]->banks[3] * 0x2000 + 0x1800 + offset];
	}

	reg = offset & 0xff;

	if (reg < 0xa0)
	{
		return k051649_waveform_r (space->machine->device("k051649"), reg);
	}
#if 0
	else if (reg >= 0xc0 && reg < 0xe0)
	{
		/* write 0xff to deformation register */
	}
#endif

	return 0xff;
}

MSX_SLOT_MAP(soundcartridge)
{
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	switch (page)
	{
	case 0:
		msx_cpu_setbank (machine, 1, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 2, state->mem + state->banks[3] * 0x2000);
		break;
	case 1:
		msx_cpu_setbank (machine, 3, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 4, state->mem + state->banks[1] * 0x2000);
		break;
	case 2:
		msx_cpu_setbank (machine, 5, state->mem + state->banks[2] * 0x2000);
		msx_cpu_setbank (machine, 6, state->mem + state->banks[3] * 0x2000);
		if (state->cart.sccp.scc_active) {
			memory_install_read8_handler(space, 0x9800, 0x9fff, 0, 0, soundcartridge_scc);
		} else {
			memory_install_read_bank(space, 0x9800, 0x9fff, 0, 0, "bank7");
		}
		if (state->cart.sccp.scc_active) {
			memory_install_read8_handler(space, 0xb800, 0xbfff, 0, 0, soundcartridge_sccp);
		} else {
			memory_install_read_bank(space, 0xb800, 0xbfff, 0, 0, "bank9");
		}
		break;
	case 3:
		msx_cpu_setbank (machine, 7, state->mem + state->banks[0] * 0x2000);
		msx_cpu_setbank (machine, 8, state->mem + state->banks[1] * 0x2000);
		break;
	}
}

MSX_SLOT_WRITE(soundcartridge)
{
	msx_state *drvstate = machine->driver_data<msx_state>();
	address_space *space = cputag_get_address_space(machine, "maincpu", ADDRESS_SPACE_PROGRAM);
	int i;

	if (addr < 0x4000)
	{
		return;
	}
	else if (addr < 0x6000)
	{
		if (state->cart.sccp.ram_mode[0])
		{
			state->mem[state->banks[0] * 0x2000 + (addr & 0x1fff)] = val;
		}
		else if (addr >= 0x5000 && addr < 0x5800)
		{
			state->banks[0] = val & state->bank_mask;
			state->cart.sccp.banks_saved[0] = val;
			slot_soundcartridge_map (machine, state, 1);
			if (drvstate->state[3] == state)
			{
				slot_soundcartridge_map (machine, state, 3);
			}
		}
	}
	else if (addr < 0x8000)
	{
		if (state->cart.sccp.ram_mode[1])
		{
			state->mem[state->banks[1] * 0x2000 + (addr & 0x1fff)] = val;
		}
		else if (addr >= 0x7000 && addr < 0x7800)
		{
			state->banks[1] = val & state->bank_mask;
			state->cart.sccp.banks_saved[1] = val;
			if (drvstate->state[3] == state)
			{
				slot_soundcartridge_map (machine, state, 3);
			}
			slot_soundcartridge_map (machine, state, 1);
		}
	}
	else if (addr < 0xa000)
	{
		if (state->cart.sccp.ram_mode[2])
		{
			state->mem[state->banks[2] * 0x2000 + (addr & 0x1fff)] = val;
		}
		else if (addr >= 0x9000 && addr < 0x9800)
		{
			state->banks[2] = val & state->bank_mask;
			state->cart.sccp.banks_saved[2] = val;
			state->cart.sccp.scc_active =
					(((val & 0x3f) == 0x3f) && !(state->cart.sccp.mode & 0x20));

			slot_soundcartridge_map (machine, state, 2);
			if (drvstate->state[0] == state)
			{
				slot_soundcartridge_map (machine, state, 0);
			}
		}
		else if (addr >= 0x9800 && state->cart.sccp.scc_active)
		{
			device_t *k051649 = space->machine->device("k051649");
			int offset = addr & 0xff;

			if (offset < 0x80)
			{
				k051649_waveform_w (k051649, offset, val);
			}
			else if (offset < 0xa0)
			{
				offset &= 0xf;

				if (offset < 0xa)
				{
					k051649_frequency_w (k051649, offset, val);
				}
				else if (offset < 0x0f)
				{
					k051649_volume_w (k051649, offset - 0xa, val);
				}
				else if (offset == 0x0f)
				{
					k051649_keyonoff_w (k051649, 0, val);
				}
			}
#if 0
			else if (offset < 0xe0)
			{
				/* write to deformation register */
			}
#endif
		}
	}
	else if (addr < 0xbffe)
	{
		if (state->cart.sccp.ram_mode[3])
		{
			state->mem[state->banks[3] * 0x2000 + (addr & 0x1fff)] = val;
		}
		else if (addr >= 0xb000 && addr < 0xb800)
		{
			state->cart.sccp.banks_saved[3] = val;
			state->banks[3] = val & state->bank_mask;
			state->cart.sccp.sccp_active =
					(val & 0x80) && (state->cart.sccp.mode & 0x20);
			slot_soundcartridge_map (machine, state, 2);
			if (drvstate->state[0] == state)
			{
				slot_soundcartridge_map (machine, state, 0);
			}
		}
		else if (addr >= 0xb800 && state->cart.sccp.sccp_active)
		{
			device_t *k051649 = space->machine->device("k051649");
			int offset = addr & 0xff;

			if (offset < 0xa0)
			{
				k052539_waveform_w (k051649, offset, val);
			}
			else if (offset < 0xc0)
			{
				offset &= 0x0f;

				if (offset < 0x0a)
				{
					k051649_frequency_w (k051649, offset, val);
				}
				else if (offset < 0x0f)
				{
					k051649_volume_w (k051649, offset - 0x0a, val);
				}
				else if (offset == 0x0f)
				{
					k051649_keyonoff_w (k051649, 0, val);
				}
			}
#if 0
			else if (offset < 0xe0)
			{
				/* write to deformation register */
			}
#endif
		}
	}
	else if (addr < 0xc000)
	{
		/* write to mode register */
		if ((state->cart.sccp.mode ^ val) & 0x20)
		{
			logerror ("soundcartrige: changed to %s mode\n",
							val & 0x20 ? "scc+" : "scc");
		}
		state->cart.sccp.mode = val;
		if (val & 0x10)
		{
			/* all ram mode */
			for (i=0; i<4; i++)
			{
				state->cart.sccp.ram_mode[i] = 1;
			}
		}
		else
		{
			state->cart.sccp.ram_mode[0] = val & 1;
			state->cart.sccp.ram_mode[1] = val & 2;
			state->cart.sccp.ram_mode[2] = (val & 4) && (val & 0x20);
			state->cart.sccp.ram_mode[3] = 0;

		}

		state->cart.sccp.scc_active =
			(((state->cart.sccp.banks_saved[2] & 0x3f) == 0x3f) &&
			!(val & 0x20));

		state->cart.sccp.sccp_active =
				((state->cart.sccp.banks_saved[3] & 0x80) && (val & 0x20));

		slot_soundcartridge_map (machine, state, 2);
	}
}

MSX_SLOT_START
	MSX_SLOT_ROM (SLOT_EMPTY, empty)
	MSX_SLOT (SLOT_MSXDOS2, msxdos2)
	MSX_SLOT (SLOT_KONAMI_SCC, konami_scc)
	MSX_SLOT (SLOT_KONAMI, konami)
	MSX_SLOT (SLOT_ASCII8, ascii8)
	MSX_SLOT (SLOT_ASCII16, ascii16)
	MSX_SLOT_SRAM (SLOT_GAMEMASTER2, gmaster2)
	MSX_SLOT_SRAM (SLOT_ASCII8_SRAM, ascii8_sram)
	MSX_SLOT_SRAM (SLOT_ASCII16_SRAM, ascii16_sram)
	MSX_SLOT (SLOT_RTYPE, rtype)
	MSX_SLOT (SLOT_MAJUTSUSHI, majutsushi)
	MSX_SLOT_SRAM (SLOT_FMPAC, fmpac)
	MSX_SLOT_ROM (SLOT_SUPERLOADRUNNER, superloadrunner)
	MSX_SLOT (SLOT_SYNTHESIZER, synthesizer)
	MSX_SLOT (SLOT_CROSS_BLAIM, crossblaim)
	MSX_SLOT (SLOT_DISK_ROM, diskrom)
	MSX_SLOT (SLOT_KOREAN_80IN1, korean80in1)
	MSX_SLOT (SLOT_KOREAN_126IN1, korean126in1)
	MSX_SLOT_ROM (SLOT_KOREAN_90IN1, korean90in1)
	MSX_SLOT (SLOT_SOUNDCARTRIDGE, soundcartridge)
	MSX_SLOT_ROM (SLOT_ROM, rom)
	MSX_SLOT_RAM (SLOT_RAM, ram)
	MSX_SLOT_RAM (SLOT_RAM_MM, rammm)
	MSX_SLOT_NULL (SLOT_CARTRIDGE1)
	MSX_SLOT_NULL (SLOT_CARTRIDGE2)
	MSX_SLOT (SLOT_DISK_ROM2, diskrom2)
MSX_SLOT_END

