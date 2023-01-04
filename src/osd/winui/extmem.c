

#include "emu.h"
#include "extmem.h"
#include "ui_temp.h"
#include "hiscore.h"
#include "driver.h"
#include "fileio.h"
#include "KailleraChat.h"

#define DEBUG 0

struct MEMORYHACK_FUNCTION MemoryHackFunction;
int MemoryHack_Player[2];
//static int flagmode;

static unsigned short OldPlayer[2];
static unsigned char OldPlay[2];	// mshvsfj4pp

void __stdcall MemoryHackDummy(void)
{
}

void __stdcall MemoryHackInit(void)
{
}
void __stdcall MemoryHackStateLoad(void)
{
}
void __stdcall MemoryHackUpdate(void)
{
}



// xmvsfregion4p
void __stdcall MemoryHackInit_xmvsfregion4p(void)
{
	//flagmode	= 0;
	memset(OldPlay, 0, 2);
	memset(MemoryHack_Player, 0, sizeof(int)*2);
}
void __stdcall MemoryHackStateLoad_xmvsfregion4p(void)
{
	MemoryHackInit_xmvsfregion4p();
}
void __stdcall MemoryHackUpdate_xmvsfregion4p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned short Player[2];
	unsigned char Mode[2];
	unsigned char Play[2];

	int i;
	for(i=0; i<2; i++)
	{
		Player[i]	 = space->read_byte(0xFF4220 + i*0x400);
		Player[i]	|= (space->read_byte(0xFF4A20 + i*0x400) << 8);
		Mode[i]		 = space->read_byte(0xFF5180 + i*0x20);
		Play[i]		 = space->read_byte(0xFF4003 + i*0x400);
	}

	for(i=0; i<2; i++)
	{

		MemoryHack_Player[i] = 0;
		
		if(Play[i])
		{
			if(Mode[i] == 1)
				MemoryHack_Player[i] = 1;
			else
			if(Mode[i] == 3)
				MemoryHack_Player[i] = 2;
			else
			if(Mode[i] == 5)
				MemoryHack_Player[i] = 0;
			else
			{
				if(Player[i] == 0x0100) //1p
					MemoryHack_Player[i] = 1;
				if(Player[i] == 0x0001) //2p
					MemoryHack_Player[i] = 2;
			}
		}
	}

	for(i=0; i<2; i++)
	{
		OldPlayer[i] = Player[i];
	}

}



// mshvsfj4p

void __stdcall MemoryHackUpdate_mshvsfj4p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned short Player[2];
	unsigned char Mode[2];
	unsigned char Play[2];

	int i;
	for(i=0; i<2; i++)
	{
		Player[i]	 = space->read_byte(0xFF3A61 + i*0x400);
		Player[i]	|= (space->read_byte(0xFF4261 + i*0x400) << 8);
		Mode[i]		 = space->read_byte(0xFF4980 + i*0x20);
		Play[i]		 = space->read_byte(0xFF3803 + i*0x400);
	}

	for(i=0; i<2; i++)
	{
		if(OldPlay[i] == 0 && Play[i] == 1)
		{
			unsigned char Select = space->read_byte(0xFF3A92 + (1-i)*0x400);
			if( Select == 0xff && Play[(1-i)] == 1)
			{
				space->write_byte(0xFF3A92 + (1-i)*0x400, 0);
			}
		}

		MemoryHack_Player[i] = 0;
		
		if(Play[i])
		{
			if(Mode[i] == 1)
				MemoryHack_Player[i] = 1;
			else
			if(Mode[i] == 3)
				MemoryHack_Player[i] = 2;
			else
			if(Mode[i] >= 5 && Mode[i] <= 8)
				MemoryHack_Player[i] = 0;
			else
			{
				if(Player[i] == 0x0100) //1p
					MemoryHack_Player[i] = 1;
				if(Player[i] == 0x0001) //2p
					MemoryHack_Player[i] = 2;
			}
		}
	}

	for(i=0; i<2; i++)
	{
		OldPlayer[i] = Player[i];
		OldPlay[i]	= Play[i];
	}


}


// mvscj4p
void __stdcall MemoryHackUpdate_mvscj4p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned short Player[2];
	unsigned char Mode[2];
	unsigned char Play[2];

	int i;
	for(i=0; i<2; i++)
	{
		int p1,p2,p3;
		Player[i]	 = space->read_byte(0xFF3281 + i*0x400);
		Player[i]	|= (space->read_byte(0xFF3A81 + i*0x400) << 8);
		Mode[i]		 = space->read_byte(0xFF4180 + i*0x30);

		p1			 = space->read_byte(0xFF3003 + i*0x400);
		p2			 = space->read_byte(0xFF3803 + i*0x400);
		p3			 = space->read_byte(0xFF32B0 + i*0x400);
		if (p1 == 1 && p2 == 1 && p3 == 0x06) 
			Play[i] = 1;
		else
			Play[i] = 0;
	}

	for(i=0; i<2; i++)
	{
		MemoryHack_Player[i] = 0;
		
		if(Play[i])
		{
			if(Mode[i] == 1)
				MemoryHack_Player[i] = 1;
			else
			if(Mode[i] == 3)
				MemoryHack_Player[i] = 2;
			else
			if(Mode[i] >= 5 && Mode[i] <= 9)
				MemoryHack_Player[i] = 0;
			else
			{
				if(Player[i] == 0x0100) //1p
					MemoryHack_Player[i] = 1;
				if(Player[i] == 0x0001) //2p
					MemoryHack_Player[i] = 2;
			}
		}
	}

	for(i=0; i<2; i++)
	{
		OldPlayer[i] = Player[i];
		OldPlay[i]	= Play[i];
	}
}

// kof98_6p
void __stdcall MemoryHackInit_kof98_6p(void)
{
	//flagmode	= 0;
	memset(MemoryHack_Player, 0, sizeof(int)*2);
}
void __stdcall MemoryHackStateLoad_kof98_6p(void)
{
	MemoryHackInit_kof98_6p();
}
void __stdcall MemoryHackUpdate_kof98_6p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned char Player[2];
	unsigned char Play1[2];
	unsigned char Play2[2];
	unsigned char Com[2];
	
	unsigned char SelectMode;
	unsigned char Mode;

	int i;
	for(i=0; i<2; i++)
	{
		Player[i]	 = space->read_byte(0x10A84D + i*0x11);
		Play1[i]	 = space->read_byte(0x10A854 + i*0x11);
		Play2[i]	 = space->read_byte(0x10A855 + i*0x11);
		Com[i]		 = space->read_byte(0x10A790 + i);
		MemoryHack_Player[i] = -1;
	}

	SelectMode	= space->read_byte(0x10EBE7);
	Mode		= space->read_byte(0x1085CE);
	

	if(Mode == 3)
	{
		MemoryHack_Player[0] = 0;
		MemoryHack_Player[1] = 0;
	}
	else if((Mode == 17 || Mode == 0) && (SelectMode & 0xc3) == 0x03)
	{
		if((SelectMode & 0x04) == 0)	// 1p
		{
			if(SelectMode & 0x10)
				MemoryHack_Player[0] = (Player[0] % 3)+1;
			else
				MemoryHack_Player[0] = 0;
		}
		if((SelectMode & 0x08) == 0)	// 2p
		{
			if(SelectMode & 0x20)
				MemoryHack_Player[1] = (Player[1] % 3)+1;
			else
				MemoryHack_Player[1] = 0;
		}
	}


	for(i=0; i<2; i++)
	{
		if(Com[i] != 0x02)
			MemoryHack_Player[i] = 0;


		if(MemoryHack_Player[i] == -1)
			switch( Player[i] )
			{
			case 0:
				MemoryHack_Player[i] = (Play1[i] % 3)+1;			break;
			case 1:
				MemoryHack_Player[i] = (Play2[i] % 3)+1;			break;
			case 2:
				{
					unsigned char a[3] = {1,1,1};
					int j;
					a[Play1[i] % 3] = 0;
					a[Play2[i] % 3] = 0;
					for(j=0; j<3; j++)
						if(a[j]) { MemoryHack_Player[i] = j+1; break;}
				}
				break;
			default:
				MemoryHack_Player[i] = 0;
				break;
			}

	}

}

// kof95_6p
void __stdcall MemoryHackUpdate_kof95_6p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned char Player[2];
	unsigned char Play1[2];
	unsigned char Play2[2];
	unsigned char Com[2];
	unsigned char Edit[2];

	unsigned char SelectMode;
	unsigned char Mode1,Mode2;
	unsigned char Game;

	int i;
	for(i=0; i<2; i++)
	{
		Player[i]	 = space->read_byte(0x10A842 + i*0x10);
		Play1[i]	 = space->read_byte(0x10A849 + i*0x10);
		Play2[i]	 = space->read_byte(0x10A84A + i*0x10);
		Com[i]		 = space->read_byte(0x10A78A + i);
		Edit[i]		 = space->read_byte(0x10A840 + i*0x10);

		MemoryHack_Player[i] = -1;
	}

	SelectMode	= space->read_byte(0x10F2F1);
	Mode1		= space->read_byte(0x10A774);
	Mode2		= space->read_byte(0x10A776);
	Game		= space->read_byte(0x10B150);


	if(Game == 0)
	{
		if(Mode1 == 05 && Mode2 == 07)
		{
			MemoryHack_Player[0] = 0;
			MemoryHack_Player[1] = 0;
		} 
		else if(SelectMode == 16)
		{
			MemoryHack_Player[0] = 0;
			MemoryHack_Player[1] = 0;
		}
		else for(i=0; i<2; i++)
		{
			if(Edit[i] == 1)
				MemoryHack_Player[i] = (Player[i] % 3)+1;
			else
				MemoryHack_Player[i] = 0;
		}
	}


	for(i=0; i<2; i++)
	{
		if(Com[i] != 2)
			MemoryHack_Player[i] = 0;

		if(MemoryHack_Player[i] == -1)
			switch( Player[i] )
			{
			case 0:
				MemoryHack_Player[i] = (Play1[i] % 3)+1;			break;
			case 1:
				MemoryHack_Player[i] = (Play2[i] % 3)+1;			break;
			case 2:
				{
					unsigned char a[3] = {1,1,1};
					int j;
					a[Play1[i] % 3] = 0;
					a[Play2[i] % 3] = 0;
					for(j=0; j<3; j++)
						if(a[j]) { MemoryHack_Player[i] = j+1; break;}
				}
				break;
			default:
				MemoryHack_Player[i] = 0;
				break;
			}
	}
}

// lbowling4p
void __stdcall MemoryHackInit_lbowling4p(void)
{
	//flagmode	= 0;
	memset(MemoryHack_Player, 0, sizeof(int)*2);
}
void __stdcall MemoryHackStateLoad_lbowling4p(void)
{
	MemoryHackInit_lbowling4p();
}
void __stdcall MemoryHackUpdate_lbowling4p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned char Player[2];

	int i;
	for(i=0; i<2; i++)
	{
		Player[i]	 = space->read_byte(0x102C0C + i);
		MemoryHack_Player[i] = 0;
	}


	for(i=0; i<2; i++)
	{
		switch( Player[i] )
		{
		case 0:
			MemoryHack_Player[i] = 1;
			break;
		case 1:
			MemoryHack_Player[i] = 1;
			break;
		case 2:
			MemoryHack_Player[i] = 2;
			break;
		case 3:
			MemoryHack_Player[i] = 2;
			break;
		default:
			//MemoryHack_Player[i] = 0;
			break;
		}
	}

}


// hyprolym4p
void __stdcall MemoryHackInit_hyprolym4p(void)
{
	//flagmode	= 0;
	memset(MemoryHack_Player, 0, sizeof(int)*2);
}
void __stdcall MemoryHackStateLoad_hyprolym4p(void)
{
	MemoryHackInit_hyprolym4p();
}
void __stdcall MemoryHackUpdate_hyprolym4p(void)
{
	address_space *space = cpu_get_address_space(k_machine->firstcpu, ADDRESS_SPACE_PROGRAM);
	unsigned char PlayerA, PlayerB;
	
	unsigned char Name;
	unsigned char Mode;
	unsigned char GameMode;

	int i;
	for(i=0; i<2; i++)
	{
		MemoryHack_Player[i] = 0;
	}
	
	PlayerA = space->read_byte(0x28FB);
	PlayerB = space->read_byte(0x28DF);

	Name = space->read_byte(0x299E);
	Mode = space->read_byte(0x2800);
	GameMode = space->read_byte(0x28E1);


	switch( Mode & 0x3 )
	{
	case 0:
		break;
	case 1:
		break;
	case 2:
		if(PlayerB & 0x2)
		{
			MemoryHack_Player[0] = 2;
			MemoryHack_Player[1] = 2;
		} else
		{
			MemoryHack_Player[0] = 1;
			MemoryHack_Player[1] = 1;
		}
		break;
	case 3:
		if((GameMode&0x3) == 1)
		{
			if(PlayerA & 0x2)
			{
				MemoryHack_Player[0] = 2;
				MemoryHack_Player[1] = 2;
			} else
			{
				MemoryHack_Player[0] = 1;
				MemoryHack_Player[1] = 1;
			}
		}
		if((GameMode&0x3) == 3)
		{
			if(PlayerB & 0x2)
			{
				MemoryHack_Player[0] = 2;
				MemoryHack_Player[1] = 2;
			} else
			{
				MemoryHack_Player[0] = 1;
				MemoryHack_Player[1] = 1;
			}
		}


	}
}


void SelectMemoryHack(const char* gamename)
{
	if (!strcmp(gamename, "xmvsfregion4p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_xmvsfregion4p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_xmvsfregion4p;
		MemoryHackFunction.Update	= MemoryHackUpdate_xmvsfregion4p;
		return;
	}

	if (!strcmp(gamename, "mshvsfj4p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_xmvsfregion4p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_xmvsfregion4p;
		MemoryHackFunction.Update	= MemoryHackUpdate_mshvsfj4p;
		return;
	}

	if (!strcmp(gamename, "mvscj4p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_xmvsfregion4p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_xmvsfregion4p;
		MemoryHackFunction.Update	= MemoryHackUpdate_mvscj4p;
		return;
	}

	if (!strcmp(gamename, "kof98_6p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_kof98_6p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_kof98_6p;
		MemoryHackFunction.Update	= MemoryHackUpdate_kof98_6p;
		return;
	}
	
	if (!strcmp(gamename, "kof95_6p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_kof98_6p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_kof98_6p;
		MemoryHackFunction.Update	= MemoryHackUpdate_kof95_6p;
		return;
	}

	if (!strcmp(gamename, "lbowling4p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_lbowling4p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_lbowling4p;
		MemoryHackFunction.Update	= MemoryHackUpdate_lbowling4p;
		return;
	}

	if (!strcmp(gamename, "hyprolym4p"))
	{
		MemoryHackFunction.Init		= MemoryHackInit_hyprolym4p;
		MemoryHackFunction.StateLoad	= MemoryHackStateLoad_hyprolym4p;
		MemoryHackFunction.Update	= MemoryHackUpdate_hyprolym4p;
		return;
	}


	MemoryHackFunction.Init		= MemoryHackDummy;
	MemoryHackFunction.StateLoad	= MemoryHackDummy;
	MemoryHackFunction.Update	= MemoryHackDummy;
}
//#############################################################################

void MemoryHack_KailleraInputVal(unsigned short *val, int numplayers)
{
	int i;

	if(MemoryHackFunction.Update == MemoryHackUpdate_xmvsfregion4p	||
		MemoryHackFunction.Update == MemoryHackUpdate_mshvsfj4p	||
		MemoryHackFunction.Update == MemoryHackUpdate_mvscj4p	||
		MemoryHackFunction.Update == MemoryHackUpdate_lbowling4p	)
	{

		switch( numplayers )
		{
		case 0:
		case 1:
		case 2:
			break;
		case 3:
			i=0;
			if(MemoryHack_Player[i] == 0)
				val[i] = val[i] | val[i+2];
			if(MemoryHack_Player[i] == 1)
				val[i] = val[i];
			if(MemoryHack_Player[i] == 2)
				val[i] = val[i+2];
			break;
		case 4:
		default:
			for(i=0; i<2; i++)
			{
				if(MemoryHack_Player[i] == 0)
					val[i] = val[i] | val[i+2];
				if(MemoryHack_Player[i] == 1)
					val[i] = val[i];
				if(MemoryHack_Player[i] == 2)
					val[i] = val[i+2];
			}
			break;
		}

	}

	if(	MemoryHackFunction.Update == MemoryHackUpdate_hyprolym4p	)
	{
		unsigned short start[4];	// X^[g{^̃vC[ʒu͕ύXȁE

		switch( numplayers )
		{
		case 0:
		case 1:
		case 2:
			break;
		case 3:
			for(i=0; i<3; i++)
				start[i] = val[i] & 0x1000;
			i=0;
			if(MemoryHack_Player[i] == 0)
				val[i] = val[i] | val[i+2];
			if(MemoryHack_Player[i] == 1)
				val[i] = val[i];
			if(MemoryHack_Player[i] == 2)
				val[i] = val[i+2];				
			
			for(i=0; i<3; i++)
				val[i] = (val[i] & ~0x1000) | start[i];
			break;
		case 4:
		default:
			for(i=0; i<4; i++)
				start[i] = val[i] & 0x1000;

			for(i=0; i<2; i++)
			{
				if(MemoryHack_Player[i] == 0)
					val[i] = val[i] | val[i+2];
				if(MemoryHack_Player[i] == 1)
					val[i] = val[i];
				if(MemoryHack_Player[i] == 2)
					val[i] = val[i+2];
			}

			for(i=0; i<4; i++)
				val[i] = (val[i] & ~0x1000) | start[i];
			break;
		}

	}

	if(MemoryHackFunction.Init == MemoryHackInit_kof98_6p)
	{
		switch( numplayers )
		{
		case 0:
		case 1:
		case 2:
			break;
		case 3:
			i=0;
			if(MemoryHack_Player[i] == 0)
				val[i] = val[i] | val[i+2];
			if(MemoryHack_Player[i] == 1)
				val[i] = val[i];
			if(MemoryHack_Player[i] == 2)
				val[i] = val[i+2];
			if(MemoryHack_Player[i] == 3)
				val[i] = val[i];
			break;
		case 4:
			for(i=0; i<2; i++)
			{
				if(MemoryHack_Player[i] == 0)
					val[i] = val[i] | val[i+2];
				if(MemoryHack_Player[i] == 1)
					val[i] = val[i];
				if(MemoryHack_Player[i] == 2)
					val[i] = val[i+2];
				if(MemoryHack_Player[i] == 3)
					val[i] = val[i];
			}
			break;
		case 5:
			i=0;
			if(MemoryHack_Player[i] == 0)
				val[i] = val[i] | val[i+2] | val[i+4];
			if(MemoryHack_Player[i] == 1)
				val[i] = val[i];
			if(MemoryHack_Player[i] == 2)
				val[i] = val[i+2];
			if(MemoryHack_Player[i] == 3)
				val[i] = val[i+4];
			i=1;
			if(MemoryHack_Player[i] == 0)
				val[i] = val[i] | val[i+2];
			if(MemoryHack_Player[i] == 1)
				val[i] = val[i];
			if(MemoryHack_Player[i] == 2)
				val[i] = val[i+2];
			if(MemoryHack_Player[i] == 3)
				val[i] = val[i];

			break;
		case 6:
		default:
			for(i=0; i<2; i++)
			{
				if(MemoryHack_Player[i] == 0)
					val[i] = val[i] | val[i+2] | val[i+4];
				if(MemoryHack_Player[i] == 1)
					val[i] = val[i];
				if(MemoryHack_Player[i] == 2)
					val[i] = val[i+2];
				if(MemoryHack_Player[i] == 3)
					val[i] = val[i+4];
			}
			break;
		}

	}
}
//#############################################################################

#define MAX_CPU 16
#define MAX_MEMORY_BLOCKS 1024

typedef struct _game_memory_list game_memory_list;
struct _game_memory_list
{
	void *					memory;
	offs_t					bytestart;
	offs_t					byteend;
	UINT8					bank;
	UINT8					spacenum;
};

static game_memory_list	GameRam_KailleraStateSave[MAX_CPU][MAX_MEMORY_BLOCKS];
static game_memory_list	GameRam_SyncCheck[MAX_CPU][MAX_MEMORY_BLOCKS];

static unsigned int nTotalGameRamSize_KailleraStateSave;
static unsigned int nTotalGameRamSize_SyncCheck;
static BOOL			bGameRamSearch_KailleraStateSave = FALSE;
static BOOL			bGameRamSearch_SyncCheck = FALSE;

//static unsigned int GameRamOverSize = 0;


#include "zlib.h" /*//kt */

#if 0
/* static data access handler constants */
enum
{
	STATIC_INVALID = 0,									/* invalid - should never be used */
	STATIC_BANK1 = 1,									/* first memory bank */
	STATIC_BANKMAX = 122,								/* last memory bank */
	STATIC_RAM,											/* RAM - reads/writes map to dynamic banks */
	STATIC_ROM,											/* ROM - reads = RAM; writes = UNMAP */
	STATIC_NOP,											/* NOP - reads = unmapped value; writes = no-op */
	STATIC_UNMAP,										/* unmapped - same as NOP except we log errors */
	STATIC_WATCHPOINT,									/* watchpoint - used internally */
	STATIC_COUNT										/* total number of static handlers */
};

/* helper macros */
#define HANDLER_IS_RAM(h)		((FPTR)(h) == STATIC_RAM)
#define HANDLER_IS_ROM(h)		((FPTR)(h) == STATIC_ROM)
#define HANDLER_IS_BANK(h)		((FPTR)(h) >= STATIC_BANK1 && (FPTR)(h) <= STATIC_BANKMAX)
#define HANDLER_IS_STATIC(h)	((FPTR)(h) < STATIC_COUNT)
#endif

#if 0
static void StrCharReplacement(char* str, const char ch1, const char ch2)
{
	while (1)
	{
		str = strchr(str, ch1);
		if (str == NULL) break;
		(*str++) = ch2;
	}
}
#endif

static void reset_table(game_memory_list *table)
{
	memset(table, 0, sizeof(game_memory_list) * MAX_MEMORY_BLOCKS);
}

static void init_game_ram_serch(running_machine *machine, game_memory_list *GameRam, unsigned int nGameRamOffset, unsigned int *pTotalGameRamSize, int use)
{
	device_t *cpu;
	int cpunum, spacenum;
	int count = 0;
	int i;

#if DEBUG
	{
		FILE *fp;

		fp = fopen("a.txt", "w");
		fprintf(fp,"%s\n", machine->gamedrv->name);
		fclose(fp);
		fp = fopen("b.txt", "w");
		fprintf(fp,"%s\n", machine->gamedrv->name);
		fclose(fp);
		fp = fopen("c.txt", "w");
		fprintf(fp,"%s\n", machine->gamedrv->name);
		fclose(fp);
	}
#endif

	(*pTotalGameRamSize) = 0;

	for (i=0; i<MAX_CPU; i++)
		reset_table((game_memory_list*)((int)GameRam + i*nGameRamOffset));

	/* loop over CPUs and address spaces */
	for (cpu = machine->m_devicelist.first(), cpunum = 0; cpu != NULL; cpu = cpu->next(), cpunum++)
	{
		const address_space *space;
		const address_map	*map;
		game_memory_list	*ext_gr = (game_memory_list*)((int)GameRam + cpunum*nGameRamOffset);

		for (spacenum = 0; spacenum <= ADDRESS_SPACE_PROGRAM; spacenum++)
		{
			const address_map_entry *entry;

			space = cpu_get_address_space(cpu, spacenum);
			map = space->map();

			/* fill in base/size entries, and handle shared memory */
			for (entry = map->m_entrylist.first(); entry != NULL; entry = entry->next())
			{
				const map_handler_data *handler;
				char source[MAX_PATH];
				int enable;
				handler = &entry->m_write;

				strcpy( source, machine->gamedrv->source_file+17);
				//StrCharReplacement( source, '\\', '/');

				enable = 0;
				if ((handler->m_type == AMH_BANK) || ((&entry->m_read)->m_type == AMH_BANK))
					enable = 1;
				if (handler->m_type == AMH_RAM)
					enable = 1;

#if DEBUG
				{
					FILE *fp;
					char desc[256];
					unsigned char *p;

					if (handler->type == AMH_BANK)
					{
						sprintf(desc, _("BANK%.2d"), ((UINT32)handler - ((UINT32)STATIC_BANK1)) + 1);
					}
					else if ((&entry->read)->type == AMH_BANK)
					{
						sprintf(desc, _("ROMBANK%.2d"), ((UINT32)entry->read.generic - ((UINT32)STATIC_BANK1)) + 1);
					}
					else if (handler->type == AMH_NOP)
					{
						strcpy(desc, "NOP   ");
					}
					else if (handler->type == AMH_RAM)
					{
						strcpy(desc, "RAM   ");
					}
					else if (handler->type == AMH_ROM)
					{
						strcpy(desc, "ROM   ");
					}
					else
					{
						strcpy(desc, "CUSTOM");
					}

					fp = fopen("a.txt", "a");
					p = entry->memory;
					fprintf(fp,"{%08X,%08X}%08X,%s\n", entry->addrstart, entry->addrend, (UINT32)(p), desc);
					fclose(fp);
				}
#endif
				//if (cpunum != 0)	enable = 0;

				if (!strcmp(source, "twin16.c"))
				{
					if (cpunum != 1)	enable = 0; else enable = 1;
					if (entry->m_addrstart < 0x060000 || entry->m_addrstart > 0x063fff)	enable = 0;
					//use = 1;
				}

				if (!strcmp(source, "taito_f3.c"))
				{
					if (entry->m_addrstart < 0x400000 || entry->m_addrstart > 0x43ffff)	enable = 0;
					//use = 1;
				}

				if (!strcmp(source, "cps1.c"))
				{
					//if (cpunum != 0)	enable = 0;
					if (entry->m_addrstart < 0xff0000 || entry->m_addrstart > 0xffffff)	enable = 0;
					//use = 1;
				}
				if (!strcmp(source, "cps2.c"))
				{
					if (cpunum != 0)	enable = 0;
					if (entry->m_addrstart < 0xff0000 || entry->m_addrstart > 0xff0000)	enable = 0;
					//use = 1;
				}

				if (!strcmp(source, "cave.c"))
				{
					//use = 1;
				}

				if (!strcmp(source, "neogeo.c"))
				{	//		{ 0x100000, 0x10ffff, MWA16_BANK1 },	// WORK RAM
					//if (entry->addrstart < 0x100000 || entry->addrstart > 0x10ffff)	enable = 0;
					//use = 1;
					if (cpunum != 0)	enable = 0;
				}

				if (!strcmp(source, "gradius3.c"))
				{
					use = 1;
				}

#if DEBUG
				if (enable && use)
				{
					FILE *fp;
					char desc[256];
					unsigned char *p;
					
					if (handler->type == AMH_BANK)
					{
						sprintf(desc, _("BANK%.2d"), ((UINT32)handler - ((UINT32)STATIC_BANK1)) + 1);
					}
					else if ((&entry->read)->type == AMH_BANK)
					{
						sprintf(desc, _("ROMBANK%.2d"), ((UINT32)entry->read.generic - ((UINT32)STATIC_BANK1)) + 1);
					}
					else if(handler->type == AMH_NOP)
					{
						strcpy(desc, "NOP   ");
					}
					else if(handler->type == AMH_RAM)
					{
						strcpy(desc, "RAM   ");
					}
					else if(handler->type == AMH_ROM)
					{
						strcpy(desc, "ROM   ");
					}
					else
					{
						strcpy(desc, "CUSTOM");
					}

					fp = fopen("b.txt", "a");
					p = entry->memory;
					fprintf(fp,"{%08X,%08X}%08X,%s\n", entry->addrstart, entry->addrend, (UINT32)(p), desc);
					fclose(fp);
				}
#endif
				if (enable && use)
				{
					unsigned int size = (entry->m_byteend - entry->m_bytestart) + 1;
					
					ext_gr->bank = 1;	// used
					if((handler->m_type == AMH_BANK) || ((&entry->m_read)->m_type == AMH_BANK))
					{
						ext_gr->bank = 2;	//bank
						//popmessage( "use BankRAM" );	// debug message
					}

					ext_gr->memory    =		entry->m_memory;
					ext_gr->bytestart =		entry->m_bytestart;
					ext_gr->byteend   =		entry->m_byteend;
					ext_gr->spacenum  =		spacenum;
					ext_gr++;

					(*pTotalGameRamSize) += size;
				}

				count++;
			}
		}
	}
}

void end_game_ram_serch(void)
{
	int i;
	if(bGameRamSearch_KailleraStateSave == TRUE)
	{
		for (i=0; i<MAX_CPU; i++)
			reset_table(&GameRam_KailleraStateSave[i][0]);
	}
	if(bGameRamSearch_SyncCheck == TRUE)
	{
		for (i=0; i<MAX_CPU; i++)
			reset_table(&GameRam_SyncCheck[i][0]);
	}

	bGameRamSearch_SyncCheck			= FALSE;
	bGameRamSearch_KailleraStateSave	= FALSE;
	nTotalGameRamSize_SyncCheck			= 0;
	nTotalGameRamSize_KailleraStateSave	= 0;


	memset (GameRam_SyncCheck, 0, sizeof(GameRam_SyncCheck));
	memset (GameRam_KailleraStateSave, 0, sizeof(GameRam_KailleraStateSave));
}

unsigned long game_ram_serch_crc32_(running_machine *machine, unsigned long crc)
{
	device_t *cpu;
	int cpunum, spacenum;
	if (bGameRamSearch_SyncCheck == FALSE)
		init_game_ram_serch(machine, &GameRam_SyncCheck[0][0], (unsigned int)&GameRam_SyncCheck[1][0] - (unsigned int)&GameRam_SyncCheck[0][0],&nTotalGameRamSize_SyncCheck, 1);
	bGameRamSearch_SyncCheck = TRUE;

	if (nTotalGameRamSize_SyncCheck == 0) return crc;

#if DEBUG
					{
						FILE *fp;

						fp = fopen("c.txt", "a");
						fprintf(fp,"\nframe:%ld\n", (long)video_screen_get_frame_number(machine->primary_screen));
						fclose(fp);
					}
#endif
	for (cpu = machine->m_devicelist.first(), cpunum = 0; cpu != NULL; cpu = cpu->next(), cpunum++)
	{
		if ( cpu->type() )
		{
			game_memory_list	* ext;
			for(ext = &GameRam_SyncCheck[cpunum][0]; ext->memory; ext++)
			{
				const unsigned int len = ext->byteend - ext->bytestart + 1;
				unsigned char *p;

				spacenum = ext->spacenum;
#if 1
				if ((int)ext->bank == 2)	//bank
				{
					p = (unsigned char *)ext->memory;
					if (p)
						crc = crc32(crc, p, len);
#if DEBUG
					{
						FILE *fp;

						fp = fopen("c.txt", "a");
						fprintf(fp,"%d,%d,%08X,%08X (BANK)\n", cpunum, spacenum, ext->bytestart, (UINT32)(p));
						fclose(fp);
					}
#endif
				} else
#endif
				if ((int)ext->bank == 1)
				{
					p = (unsigned char *)ext->memory;
					if (p)
						crc = crc32(crc, p, len);
#if DEBUG
					{
						FILE *fp;

						fp = fopen("c.txt", "a");
						fprintf(fp,"%d,%d,%08X,%08X\n", cpunum, spacenum, ext->bytestart, (UINT32)(p));
						fclose(fp);
					}
#endif
				}
			}
		}
	}

	return crc;
}

unsigned long game_ram_serch_crc32_kaillera_state_save(running_machine *machine, unsigned long crc)
{
	device_t *cpu;
	int cpunum;

	if (bGameRamSearch_KailleraStateSave == FALSE)
		init_game_ram_serch(machine, &GameRam_KailleraStateSave[0][0], (unsigned int)&GameRam_KailleraStateSave[1][0] - (unsigned int)&GameRam_KailleraStateSave[0][0], &nTotalGameRamSize_KailleraStateSave, 0);
	bGameRamSearch_KailleraStateSave = TRUE;

	if (nTotalGameRamSize_KailleraStateSave == 0) return crc;

	for (cpu = machine->m_devicelist.first(), cpunum = 0; cpu != NULL; cpu = cpu->next(), cpunum++)
	{
		if ( cpu->type() )
		{
			game_memory_list	* ext;
			for(ext = &GameRam_KailleraStateSave[cpunum][0]; ext->memory; ext++)
			{
				unsigned char *p;
				const unsigned int len = ext->byteend - ext->bytestart + 1;

				p = (unsigned char *)ext->memory;
				crc = crc32(crc, p, len);
			}
		}
	}
	return crc;
}


