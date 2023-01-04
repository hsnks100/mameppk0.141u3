
struct MEMORYHACK_FUNCTION
{
	void (__stdcall *Init)(void);
	void (__stdcall *StateLoad)(void);
	void (__stdcall *Update)(void);
};

extern struct MEMORYHACK_FUNCTION MemoryHackFunction;


void __stdcall MemoryHackDummy(void);

void __stdcall MemoryHackInit(void);
void __stdcall MemoryHackStateLoad(void);
void __stdcall MemoryHackUpdate(void);

void __stdcall MemoryHackInit_lbowling4p(void);
void __stdcall MemoryHackStateLoad_lbowling4p(void);
void __stdcall MemoryHackUpdate_lbowling4p(void);


void SelectMemoryHack(const char* gamename);

void MemoryHack_KailleraInputVal(unsigned short *val, int numplayers);

//###########################################################3

unsigned long game_ram_serch_crc32_(running_machine *machine, unsigned long crc);
unsigned long game_ram_serch_crc32_kaillera_state_save(running_machine *machine, unsigned long crc);
int get_game_ram_serch_cpu(unsigned int *size, unsigned int *bankcpu, unsigned int *bankcount);
void end_game_ram_serch(void);
