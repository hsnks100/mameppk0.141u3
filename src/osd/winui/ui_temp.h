#define UCLOCKS_PER_SEC 1000000


#define KAILLERA_MAX_PLAYER 16
#define KAILLERA_VAL_LEN 2

//#if KAILLERA_VAL_LEN == 2
#define KAILLERA_VAL_INT unsigned short

//#if KAILLERA_VAL_LEN == 4
//#define KAILLERA_VAL_INT unsigned long



#define KAILLERA_IPT_UI_GET(i) ((val[i] >> usedkeys[0]) & 0x1) |\
								((val[i] >> (usedkeys[1]-1)) & 0x2) |\
								((val[i] >> (usedkeys[2]-2)) & 0x4)

#define KAILLERA_IPT_UI_SET(ipt) ((ipt & 0x1) << usedkeys[0]) |\
								 ((ipt & 0x2) << (usedkeys[1]-1)) |\
								 ((ipt & 0x4) << (usedkeys[2]-2))


struct KAILLERA_START_OPTION
{
	int player, numplayers;
	int gameinputmin;

	int auto_end;
	int send_file_speed;
	int autosave_time_interval;

	unsigned int lost_connection_time;
	int lost_connection_operation;
};
struct KAILLERA_PLAYER_OPTION
{
	int max;
	char drop_player[8];	//8bit x 8  最大64人分
	int subplayer;
	int playercontrol;

	int waittimemode;	// 時間調整レベル
	int sendfilespeed;

	unsigned long chatsend_timelag;
};
extern struct KAILLERA_START_OPTION KailleraStartOption;
extern struct KAILLERA_PLAYER_OPTION KailleraPlayerOption;
extern int playernmb[KAILLERA_MAX_PLAYER];
extern unsigned int Kaillera_StatusFlag;
#define KAILLERA_STATUSFLAG_2INPUTSTART			1
#define KAILLERA_STATUSFLAG_LOST_CONNECTION		0x100


extern int input_ui_temp;
extern int input_ui_temp_dat[8];
extern int perform_ui_count;
extern char perform_ui_statesave_file[256];
extern int perform_ui_statesave_file_size;
extern int perform_ui_statesave_file_fp;
extern char trctemp_statesave_file[256];	// trctempにコピーされたstateファイル
extern int trctemp_statesave_file_size;

extern int maxplayer;
extern unsigned long synccount;








// アナログ入力制御
//#define KAILLERA_ANALOG_LEN 4
#define KAILLERA_ANALOG_LEN 2
#define KAILLERA_MAX_ANALOG_INPUT_PORTS 20
extern int Kaillera_analog_port;// このゲームで1pが使用するアナログ入力の数
extern int Kaillera_Val_Len;
#if KAILLERA_ANALOG_LEN == 2
extern short kinput_analog_delta[KAILLERA_MAX_ANALOG_INPUT_PORTS][2];
#else
extern int kinput_analog_delta[KAILLERA_MAX_ANALOG_INPUT_PORTS][2];
#endif

// ステートセーブ制御
extern int Kaillera_StateSave_CRC;
extern int Kaillera_StateSave_Count;

extern int Kaillera_StateSave_TimeRemainder;
extern unsigned int Kaillera_StateSave_Flags;
extern int Kaillera_StateSave_Retry;
extern int Kaillera_StateSave_file;
extern int Kaillera_StateSave_SelectFile; // bool
//#define KAILLERA_STATESAVE_NORMAL_DELAYTIME 100
#define KAILLERA_STATESAVE_NORMAL_DELAYTIME 256

#define KAILLERA_STATESAVE_AUTOSAVE 0x00000001
//#define KAILLERA_FLAGS_DIPSWITCH 0x00000002
#define KAILLERA_FLAGS_RESET_MACHINE 0x80000000

//
struct KAILLERA_CHATDATA_PREPARATIONCHECK
{
	int nmb;
	char *str;
	int count;
	int timeremainder;
	int addtime, maxtime;
	int flag;
	void (__cdecl *Callback)(int flag);
	void (__cdecl *Callback_Start)(int data);
	void (__cdecl *Callback_Update)(int flag, unsigned long *data);
};

extern struct KAILLERA_CHATDATA_PREPARATIONCHECK KailleraChatdataPreparationcheck;

// CRC
extern unsigned long Kaillera_Inp_CRC;
extern unsigned long Kaillera_Inp_StateSave_CRC;
extern int Kaillera_Inp_InitSleepTime;

// チャットを使ってファイル転送
extern unsigned long Kaillera_Send_Pos;
extern unsigned long Kaillera_Send_Len;				// 圧縮後のサイズ
extern unsigned long Kaillera_Send_DecompressLen;	// 圧縮前のサイズ
extern unsigned long Kaillera_Send_CRC;				// 圧縮前のCRC
extern char			*Kaillera_Send_lpBuf;
extern unsigned int Kaillera_Send_Flags;
extern int			Kaillera_Send_SleepTime;

// オーバークロック
extern int Kaillera_Overclock_Flags;
extern int Kaillera_Overclock_Multiple;



void player_renmb(void *val,int len);
void playernmb_set(int nmb, int to);
void playernmb_clear(void);
void getmessage_playernmb(char *dst, int recvPlayer, int player, const char *str, int n2inp);

//		重複位置用。
extern int playernmb_dup[KAILLERA_MAX_PLAYER];
void player_renmb_dup(char *chval, unsigned short valxor, int players, int maxmj);
void playernmb_dup_set(int nmb, int to);
void playernmb_dup_clear(void);
void getmessage_playernmb_dup(char *dst, int recvPlayer, int player, const char *str, int n2inp);

void player_val_clear(char *chval, unsigned short valxor, int players, int maxmj);


int* usedkeys_get(void);
void usedkeysMask_set(unsigned short mask);
unsigned short usedkeysMask_get(void);


// kChatDataBuf[0]	nnnnnnnn 00000000 cccccccc cccccccc
// c = COMMAND
// n = PLAYERNMB
#define KAILLERA_CHATDATA_GET_PLAYERNMB(buf) (((buf) >> 24) & 0xff)
#define KAILLERA_CHATDATA_GET_COMMAND(buf) ((buf) & 0xffff)
extern unsigned long kChatDataBuf[64];
extern char *lpkChatDatabit;
extern unsigned long kChatDatabitLen;
char* kChatData(void *src, int len);
int kChatReData(void *dst, char *src);
char* kChatDatabit(void *src, int len);
unsigned long kChatReDatabit(void *dst, char *src);
char* IntToHex(int src);
short ByteToHex(char ch);
char HexToByte(char *src);
char* IntToBit(int src);



struct KAILLERA_SYNCCHECK
{
	unsigned long basepos;
	unsigned long pos;
	unsigned int step;
	unsigned int count;
	unsigned int totalcount;

	unsigned long crc[64][128];	//64人分のCRC。１人最大128個まで。
};
extern struct KAILLERA_SYNCCHECK KailleraSyncCheck;

// ゲーム名制御
void* Emerald_filename_cmp(const void *name, int *flag);
// サウンドのサンプリングレート制御
unsigned int Emerald_get_sample_rate(const void *name, unsigned int sample_rate);

void input_temp_Clear(void);
void Kaillera_Emerald_End(void);


#include <windows.h>
#include "kailleraclient.h"

extern HANDLE hProcess_KailleraIPC;
extern int nKailleraGameCallBack_IPC;
extern unsigned long dwID_Thread_KailleraIPC;


int load_16to8_bitmap(void *filename, RGBQUAD *rgbq, char* cnv);

void PreparationcheckClear(void);
void PreparationcheckReset(void);
void __cdecl PreparationcheckNull(int flag);
void __cdecl PreparationcheckNull_Update(int flag, unsigned long *data);
extern void __cdecl SendStateSaveFile(int flag);
extern void __cdecl SendStateSaveFile_Update(int flag, unsigned long *data);
extern void __cdecl SendDipSwitch(int flag);
extern void __cdecl Send2InputStart(int flag);
extern void __cdecl SendSyncCheck(int flag);
extern void __cdecl SendSyncCheck_Update(int flag, unsigned long *data);
extern void __cdecl SendOverclockParam(int flag);


void KailleraLostConnectionCheck(int command);
void KailleraLostConnection(void);

void kAnalog_input_port_clear(void);
void kAnalog_input_port_end(void);
