#include "emu.h"
#include "ui_temp.h"
#include <stdlib.h>
#include <stdio.h>
#include "bitmask.h"
#include "mui_opts.h"
#include "KailleraChat.h"
#include "uilang.h"
#include "ui.h"
#include "winui.h"
#include "strconv.h"
#include "translate.h"

#include "window.h"
#include "video.h"
#define win_window_mode video_config.windowed


/*-------------------------------------------------*/
int playernmb[KAILLERA_MAX_PLAYER];
struct KAILLERA_START_OPTION KailleraStartOption;
struct KAILLERA_PLAYER_OPTION KailleraPlayerOption;
unsigned int Kaillera_StatusFlag;

struct KAILLERA_SYNCCHECK KailleraSyncCheck;

int input_ui_temp;
int input_ui_temp_dat[8];
int perform_ui_count;
char perform_ui_statesave_file[256];
int perform_ui_statesave_file_size;
int perform_ui_statesave_file_fp;
char trctemp_statesave_file[256];	// trctempにコピーされたstateファイル
int trctemp_statesave_file_size;
int maxplayer = 0;
unsigned long synccount = 0;
int syncmode = 0;


void kAnalog_input_port_clear(void){}
void kAnalog_input_port_end(void){}
//void Get_bIsWindow(){}
//void ChangeDisplayMode(){}
//void quiting(){}
//void KailleraChatEnd(){}

void player_renmb(void *val,int len)
{
	char *lp = (char *)val;
	unsigned char chvaltemp[32*KAILLERA_MAX_PLAYER];
	int i;
	memcpy(chvaltemp, lp, KAILLERA_MAX_PLAYER*len);

	for(i=0; i<KAILLERA_MAX_PLAYER; i++)
		memcpy(lp + i*len, chvaltemp + (playernmb[i])*len, len);
	
}

void playernmb_set(int nmb, int to)
{
	int nmbtemp[KAILLERA_MAX_PLAYER];
	int i,j;
	memcpy(nmbtemp, playernmb, KAILLERA_MAX_PLAYER*4);

	memset(playernmb, 0xff, KAILLERA_MAX_PLAYER*4);

	nmb = nmbtemp[nmb];
	playernmb[to] = nmb;

	j=0;
	for(i=0; i<KAILLERA_MAX_PLAYER; i++)
	{
		if(to==i) i++;
		if(playernmb[i] == 0xffffffff) {
			if(nmbtemp[j] == nmb) j++;
			playernmb[i] = nmbtemp[j++];
		}
	}

}

int playernmb_get(int nmb)
{
	extern int kPlayerDup;
	if (kPlayerDup)
	{
		return playernmb_dup[nmb];
	}
	else
	{
		return playernmb[nmb];
	}
}

void playernmb_clear(void)
{
	int i;

	for(i=0; i<KAILLERA_MAX_PLAYER; i++)
		playernmb[i] = i;

	if( KailleraPlayerOption.subplayer ) {
		for(i=0; i<KailleraStartOption.numplayers; i++)
			playernmb[i] = i*2;
		for(i=0; i<KailleraStartOption.numplayers; i++)
			playernmb[i + KailleraStartOption.numplayers] = i*2+1;
		return;
	}

}


void getmessage_playernmb(char *dst, int recvPlayer, int player, const char *str, int n2inp)
{
	int j,jj,pl;
	char s[256];
	char *utf8_string;

	char chPlNmb[10+26];
	for(jj=0;jj<10;jj++)
		chPlNmb[jj] = jj + '0';
	for(jj=0;jj<26;jj++)
		chPlNmb[jj+10] = jj + 'A';

	
	if (KailleraPlayerOption.subplayer )
	{
		pl = ((player & 0x1) ? (KailleraStartOption.numplayers +(player>>1)):(player>>1)) + 1;
		for(j=0; j<recvPlayer; j++)
		{
			if (playernmb[j] == (KailleraStartOption.player-1)*2+KailleraPlayerOption.playercontrol) s[j*2] = '*';
			else if (n2inp && playernmb[j] == (KailleraStartOption.player-1)*2+KailleraPlayerOption.subplayer) s[j*2] = '+'; //2inputの位置変更用
			else s[j*2] = chPlNmb[((playernmb[j] & 0x1) ? (KailleraStartOption.numplayers +(playernmb[j]>>1)):(playernmb[j]>>1)) + 1];
			s[j*2+1] = ',';
		}
		s[(KailleraStartOption.numplayers-1)*2 + 1] = ' ';
	} else
	{
		pl = player+1;
		for(j=0; j<recvPlayer; j++)
		{
			if (playernmb[j] == KailleraStartOption.player-1) s[j*2] = '*';
			else s[j*2] = chPlNmb[playernmb[j]+1];
			s[j*2+1] = ',';
		}
	}
	s[recvPlayer*2 - 1] = 0;


	utf8_string = utf8_from_wstring(_UIW(L"%dp Position %s %s"));
	if (utf8_string)
	{
		sprintf( dst, utf8_string, pl, str, s);
		free(utf8_string);
	}
}



/*-------------------------------------------------*/
//		重複位置用。
int playernmb_dup[KAILLERA_MAX_PLAYER];

static unsigned short KailleraCount;
void player_renmb_dup(char *chval, unsigned short valxor, int players, int maxmj)
{
	unsigned char tmp_chval[32*KAILLERA_MAX_PLAYER];
	unsigned short val;
	int i,j,len;

	len = KAILLERA_VAL_LEN + Kaillera_analog_port * KAILLERA_ANALOG_LEN;
	if( maxmj )	len++;
	
	memcpy(tmp_chval, chval, len*players);

	for(i=0; i<players; i++)
	{
		*((short*)&chval[i*len]) = valxor;
		for(j=0; j<Kaillera_analog_port; j++)
			*((short*)&chval[i*len + KAILLERA_VAL_LEN + j*KAILLERA_ANALOG_LEN]) = 0;
		if(maxmj)	chval[i*len + KAILLERA_VAL_LEN + (Kaillera_analog_port * KAILLERA_ANALOG_LEN)] = 0;
	}

	for(i=0; i<players; i++)
	{
		int tmp;
		val = (*((unsigned short*)&tmp_chval[i*len])) ^ valxor;
		val |= ((*((unsigned short*)&chval[playernmb_dup[i]*len])) ^ valxor);
		*((unsigned short*)&chval[playernmb_dup[i]*len]) = val ^ valxor;

		for(j=0; j<Kaillera_analog_port; j++)
		{
			tmp = *((short*)&chval[playernmb_dup[i]*len + KAILLERA_VAL_LEN + j*KAILLERA_ANALOG_LEN]);
			tmp += (int)(*((short*)&tmp_chval[i*len + KAILLERA_VAL_LEN + j*KAILLERA_ANALOG_LEN]));

			if(tmp > 32767) tmp = 32767;
			else if(tmp < -32768) tmp = -32768;

			*((short*)&chval[playernmb_dup[i]*len + KAILLERA_VAL_LEN + j*KAILLERA_ANALOG_LEN]) = (short)tmp;
		}

		if(maxmj)
			if(chval[playernmb_dup[i]*len + KAILLERA_VAL_LEN + (Kaillera_analog_port * KAILLERA_ANALOG_LEN)] == 0)
				chval[playernmb_dup[i]*len + KAILLERA_VAL_LEN + (Kaillera_analog_port * KAILLERA_ANALOG_LEN)] = tmp_chval[i*len + KAILLERA_VAL_LEN + (Kaillera_analog_port * KAILLERA_ANALOG_LEN)];
	}

}

void playernmb_dup_set(int nmb, int to)
{
	playernmb_dup[nmb] = to;
}


void playernmb_dup_clear(void)
{
	int i;
	for(i=0; i<KAILLERA_MAX_PLAYER; i++)
		playernmb_dup[i] = i;

	if( KailleraPlayerOption.subplayer ) {
		for(i=0; i<KailleraStartOption.numplayers; i++)
			playernmb_dup[i*2] = i;
		for(i=0; i<KailleraStartOption.numplayers; i++)
			playernmb_dup[i*2+1] = i + KailleraStartOption.numplayers;
		return;
	}
}

void getmessage_playernmb_dup(char *dst, int recvPlayer, int player, const char *str, int n2inp)
{
	int jj,ii,pl;
	char s[256];
	int pn[KAILLERA_MAX_PLAYER],plpos[KAILLERA_MAX_PLAYER];
	char *utf8_string;

	//char chPlNmb[10+6];
	char chPlNmb[10+26];
	for(jj=0;jj<10;jj++)
		chPlNmb[jj] = jj + '0';
	//for(jj=0;jj<6;jj++)
	for(jj=0;jj<26;jj++)
		chPlNmb[jj+10] = jj + 'A';

	memset(pn, 0, sizeof(int)*recvPlayer);
	for(jj=0; jj<recvPlayer; jj++)
		pn[playernmb_dup[jj]]++;

	ii=0;
	for(jj=0; jj<recvPlayer; jj++)
	{
		plpos[jj] = ii;
		ii += pn[jj];
		s[ii] = ',';
		ii++;
	}
					
	if( KailleraPlayerOption.subplayer )
	{
		int p[KAILLERA_MAX_PLAYER];
		for(jj=0; jj<KAILLERA_MAX_PLAYER; jj++)
		{
			if(jj & 0x1)p[jj] = KailleraStartOption.numplayers + (jj >> 1) + 1;
			else p[jj] = (jj >> 1) + 1;
		}
		pl=p[player];
		for(jj=recvPlayer-1; jj>=0; jj--)
		{
			if (jj == (KailleraStartOption.player-1)*2+KailleraPlayerOption.playercontrol) s[plpos[playernmb_dup[jj]] + (pn[playernmb_dup[jj]]-1)] = '*';
			else if (n2inp && jj == (KailleraStartOption.player-1)*2+KailleraPlayerOption.subplayer) s[plpos[playernmb_dup[jj]] + (pn[playernmb_dup[jj]]-1)] = '+';	//2inputの位置変更用
			else s[plpos[playernmb_dup[jj]] + (pn[playernmb_dup[jj]]-1)] = chPlNmb[p[jj]];
			pn[playernmb_dup[jj]]--;
		}
	} else
	{
		pl=player+1;
		for(jj=recvPlayer-1; jj>=0; jj--)
		{
			if(jj == (KailleraStartOption.player-1)) s[plpos[playernmb_dup[jj]] + (pn[playernmb_dup[jj]]-1)] = '*';
			else s[plpos[playernmb_dup[jj]] + (pn[playernmb_dup[jj]]-1)] = chPlNmb[jj+1];
			pn[playernmb_dup[jj]]--;
		}
	}
	s[ii-1] = 0;

	utf8_string = utf8_from_wstring(_UIW(L"%dp Position %s [%s]"));
	if (utf8_string)
	{
		sprintf( dst, utf8_string, pl, str, s);
		free(utf8_string);
	}
}




void player_val_clear(char *chval, unsigned short valxor, int players, int maxmj)
{
	//unsigned short val;
	int i,j,len;

	len = KAILLERA_VAL_LEN + Kaillera_analog_port * KAILLERA_ANALOG_LEN;
	if( maxmj )	len++;

	for(i=0; i<players; i++)
	{
		*((short*)&chval[i*len]) = valxor;
		for(j=0; j<Kaillera_analog_port; j++)
			*((short*)&chval[i*len + KAILLERA_VAL_LEN + j*KAILLERA_ANALOG_LEN]) = 0;
		if(maxmj)	chval[i*len + KAILLERA_VAL_LEN + (Kaillera_analog_port * KAILLERA_ANALOG_LEN)] = 0;
	}
}


/*-------------------------------------------------*/

static int usedkeys[16];
//static int usedkeyCount = 0;
static unsigned short usedkeyMask = ~0;

int* usedkeys_get(void)
{
	return usedkeys;
}

void usedkeysMask_set(unsigned short mask)
{
	usedkeyMask = mask;
	return;
}
unsigned short usedkeysMask_get(void)
{
	return usedkeyMask;
}

/*-------------------------------------------------*/

/*-------------------------------------------------*/
// アナログ入力制御
int Kaillera_analog_port;// このゲームで1pが使用するアナログ入力の数
int Kaillera_Val_Len;
//extern void kAnalog_input_port_clear(void);

#if KAILLERA_ANALOG_LEN == 2
short kinput_analog_delta[KAILLERA_MAX_ANALOG_INPUT_PORTS][2];
#else
int kinput_analog_delta[KAILLERA_MAX_ANALOG_INPUT_PORTS][2];
#endif

/*-------------------------------------------------*/
// ステートセーブ制御
int Kaillera_StateSave_CRC;
int Kaillera_StateSave_Count;

int Kaillera_StateSave_TimeRemainder;
unsigned int Kaillera_StateSave_Flags;
int Kaillera_StateSave_Retry;
int Kaillera_StateSave_file;
int Kaillera_StateSave_SelectFile;

/*-------------------------------------------------*/
// CRC
unsigned long Kaillera_Inp_CRC;
unsigned long Kaillera_Inp_StateSave_CRC;
int Kaillera_Inp_InitSleepTime;

/*-------------------------------------------------*/
// オーバークロック
int Kaillera_Overclock_Flags;
int Kaillera_Overclock_Multiple;


/*-------------------------------------------------*/
// チャットデータ送信を使ってファイル転送
unsigned long Kaillera_Send_Pos;
unsigned long Kaillera_Send_Len;
unsigned long Kaillera_Send_DecompressLen;	// 圧縮前のサイズ
unsigned long Kaillera_Send_CRC;			// 圧縮前のCRC
char		*Kaillera_Send_lpBuf;
unsigned int Kaillera_Send_Flags;
int			 Kaillera_Send_SleepTime;

/*-------------------------------------------------*/
// チャットを使ってデータ送受信

unsigned long kChatDataBuf[64];
char *lpkChatDatabit;
unsigned long kChatDatabitLen;

char* kChatData(void *src, int len)
{
	int i;
	char *s = (char*)src;
	short *dst;
	static char tmp[512];
	tmp[0] = 0x0d;
	tmp[1] = 0x0a;
	tmp[2] = 0x44;
	tmp[3] = 0x41;

	if( len>=250 )return 0;//Error

	dst = (short*)(&tmp[4]);
	for(i=0; i<len; i++)
	{
		(*dst++) = ByteToHex((*s++));
	}

	(*dst) = 0;// 文字列の最後
	return tmp;
}



int kChatReData(void *dst, char *src)	//戻り値は、変換後のバイト数
{
	int len = 0;
	char *ds = (char*)dst;

	while ( (*src) != 0 )
	{
		(*ds++) = HexToByte(src);
		src+=2;
		len++;
	}
	return len;
}

char* kChatDatabit(void *src, int len)
{
	int i,j;
	char *s = (char*)src;
	char *dst;
	static char tmp[4096];
	int bitpos = 0;
	tmp[0] = 0x0d;
	tmp[1] = 0x0a;
	tmp[2] = 0x44;
	tmp[3] = 0x42;

	if( len>=4000 )return 0;//Error

	dst = &tmp[4];
	(*dst) = (char)0x80;// クリア
	for(i=0; i<len; i++) {
		for(j=0; j<8; j++) 
		{
			(*dst) |= (((*s)>> j & 0x1) << bitpos);
			bitpos = bitpos + 1;
			if((bitpos & 0x7) == 7) bitpos = bitpos + 1; // 未使用ビットを抜かす。
			if(bitpos & ~0x7) {
				bitpos &= 0x7;
				(*(++dst)) = (char)0x80; // クリア
			}
		}
		s++;
	}
	if(bitpos & 0x7) dst++;
	(*dst) = 0;// 文字列の最後
	return tmp;
}



unsigned long kChatReDatabit(void *dst, char *src)
{
	int j;
	char *ds = (char*)dst;
	int bitpos = 0;
	unsigned long len = 0;

	while ( (bitpos == 0 && (*src) != 0) || (bitpos != 0 && *(src+1) != 0))
	{
		(*ds) = 0; //クリア
		for(j=0; j<8; j++)
		{
			(*ds) |= ((*src)>> bitpos & 0x1) << j;
			bitpos = bitpos + 1;
			if((bitpos & 0x7) == 7) bitpos = bitpos + 1; // 未使用ビットを抜かす。
			if(bitpos & ~0x7) {
				bitpos &= 0x7;
				src++;
			}
		}
		ds++;
		len++;
	}
	return len;
}

char* IntToHex(int src)
{
	int i;
	char *s = ((char*)(&src))+3;
	short *dst;
	static char tmp[10];

	dst = (short*)(&tmp[0]);
	for(i=0; i<4; i++)
	{
		(*dst++) = ByteToHex((*s--));
	}
	(*dst) = 0;// 文字列の最後
	return tmp;
}

// １バイトデータを２バイト16進数文字に
short ByteToHex(char ch)
{

	short st;
	char m;
	m = (ch & 0xf) + '0';
	if(m > '9') m=m-'0'-0xa + 'a';
	st = (short)m << 8;

	m = (ch>>4 & 0xf) + '0';
	if(m > '9') m=m-'0'-0xa + 'a';

	st |= (short)m;

	return st;
}
char HexToByte(char *src)
{
	char ch;
	char m;

	m = (*src);
	if( m >= '0' && m <= '9' )
		m -= '0';
	else if( m >= 'a' && m <= 'f' )
		m=m-'a' + 0xa;
	else return 0;
	ch = m << 4;

	m = (*(src+1));
	if( m >= '0' && m <= '9' )
		m -= '0';
	else if( m >= 'a' && m <= 'f' )
		m=m-'a' + 0xa;
	else return 0;
	ch |= m;

	return ch;
}

char* IntToBit(int src)
{
	static char ch[33];
	int i;

	for(i=0; i<32; i++)
		ch[i] = ((src >> (31-i)) & 0x1) + '0';

	ch[32] = 0;
	return ch;
}

#if 0
/*-------------------------------------------------*/
// ゲーム名制御
void* Emerald_filename_cmp(const void *name, int *flag)
{
	char *rename = (char*)name;
	// gameinputmin		= 0x1
	// subplayer		= 0x2
	// KAnalogCtrl		= 0x4

	(*flag) = 0;
	if (!strcmp(name, "avspj3p2input"))		{	(*flag) |= 0x2; rename = "avspj3p"		;}
	if (!strcmp(name, "ddsomjfix2input"))	{	(*flag) |= 0x2; rename = "ddsomjfix"	;}
	if (!strcmp(name, "ddtodj2input"))		{	(*flag) |= 0x2; rename = "ddtodj"		;}
	if (!strcmp(name, "pgear2input"))		{	(*flag) |= 0x2; rename = "pgear"		;}

	if (!strcmp(name, "captcomj3p2input"))	{	(*flag) |= 0x2; rename = "captcomj3p"	;}
	if (!strcmp(name, "captcomj4p2input"))	{	(*flag) |= 0x2; rename = "captcomj4p"	;}
	if (!strcmp(name, "mbomberj2input"))	{	(*flag) |= 0x2; rename = "mbomberj"		;}
	if (!strcmp(name, "mbombrdj2input"))	{	(*flag) |= 0x2; rename = "mbombrdj"		;}
	if (!strcmp(name, "wofj2input"))		{	(*flag) |= 0x2; rename = "wofj"			;}

	if (!strcmp(name, "bbmanw2input"))		{	(*flag) |= 0x2; rename = "bbmanw"		;}

	if (!strcmp(name, "wbeachvl2input"))	{	(*flag) |= 0x2; rename = "wbeachvl"		;}

	if (!strcmp(name, "s1945pn"))			{	(*flag) |= 0x0; rename = "s1945p"		;}
	if (!strcmp(name, "preisl2n"))			{	(*flag) |= 0x0; rename = "preisle2"		;}
	if (!strcmp(name, "nitdn"))				{	(*flag) |= 0x0; rename = "nitd"			;}
	if (!strcmp(name, "ganryun"))			{	(*flag) |= 0x0; rename = "ganryu"		;}
	if (!strcmp(name, "kof99nd"))			{	(*flag) |= 0x0; rename = "kof99n"		;}
	if (!strcmp(name, "garoun"))			{	(*flag) |= 0x0; rename = "garou"		;}
	if (!strcmp(name, "mslug3nd"))			{	(*flag) |= 0x0; rename = "mslug3n"		;}
	if (!strcmp(name, "kof2knd"))			{	(*flag) |= 0x0; rename = "kof2000n"		;}

	return (void*)rename;
}
// サウンドのサンプリングレート制御
unsigned int Emerald_get_sample_rate(const void *name, unsigned int sample_rate)
{
	int tmp = 0;
	if (!strcmp(name, "ddonpach_fix"))	tmp = 1;
	if (!strcmp(name, "dfeveron_fix"))	tmp = 1;
	if (!strcmp(name, "esprade_fix"))	tmp = 1;
	if (!strcmp(name, "espradej_fix"))	tmp = 1;
	if (!strcmp(name, "uopoko_fix"))	tmp = 1;
	if (!strcmp(name, "guwange_fix"))	tmp = 1;
	if (!strcmp(name, "guwange2p"))		tmp = 1;

	if(tmp)
	{
		if(sample_rate == 11025)
			sample_rate = 11024;
		if(sample_rate == 22050)
			sample_rate = 22051;
		if(sample_rate == 44100)
			sample_rate = 44101;
	}
	return sample_rate;
}
#endif

/*-------------------------------------------------*/
void input_temp_Clear(void)
{
	int i;
	synccount = 0;
	maxplayer = 0;


	lpkChatDatabit = 0; // NULL
	kChatDatabitLen = 0;

	Kaillera_StateSave_Count = 0;
	Kaillera_StateSave_TimeRemainder = KAILLERA_STATESAVE_NORMAL_DELAYTIME-1;
	Kaillera_StateSave_Flags = 0;
	Kaillera_StateSave_Retry = 0;
	Kaillera_StateSave_file = 'a';
	Kaillera_StateSave_SelectFile = 0;

	Kaillera_Inp_CRC = 0;
	Kaillera_Inp_StateSave_CRC = 0;
	Kaillera_Inp_InitSleepTime = KAILLERA_STATESAVE_NORMAL_DELAYTIME-1;


	Kaillera_Send_Flags = 0;
	Kaillera_Send_SleepTime = 0;


	Kaillera_Overclock_Flags = 0;
	Kaillera_Overclock_Multiple = 2;


	KailleraPlayerOption.subplayer		= 0;
	KailleraPlayerOption.playercontrol	= 0;
	KailleraPlayerOption.chatsend_timelag	= 60;	//初期値１秒

	memset (&KailleraSyncCheck, 0, sizeof(KailleraSyncCheck));

	Kaillera_analog_port = 0;
	for(i=0; i<KAILLERA_MAX_ANALOG_INPUT_PORTS; i++) {
		kinput_analog_delta[i][0] = 0;
		kinput_analog_delta[i][1] = 0;
	}
	kAnalog_input_port_clear();


	playernmb_clear();
	playernmb_dup_clear();
	KailleraCount = 0;

	for(i=0; i<16; i++)
		usedkeys[i] = -1;
	usedkeyMask = ~0;
}

#include "extmem.h"
void Kaillera_Emerald_End(void)
{

	if( lpkChatDatabit )
		free( lpkChatDatabit );
	lpkChatDatabit = 0;

	end_game_ram_serch();
}



// ################################


HANDLE hProcess_KailleraIPC = NULL;
int nKailleraGameCallBack_IPC = 0;
unsigned long dwID_Thread_KailleraIPC = 0;


#if 0
#if 1
#include <fcntl.h>
#include <io.h>
#include <sys\stat.h>
#endif
int load_16to8_bitmap(void *filename, RGBQUAD *rgbq, char* cnv)
{
	int bmpfile = -1;
	char *bm;
	BITMAPINFOHEADER    bmiHeader;
	BITMAPFILEHEADER	bmfh;
	int y;

	bmpfile = open(filename, O_RDONLY | O_BINARY);
	if (bmpfile == -1) return -1;

	read(bmpfile, &bmfh, sizeof(BITMAPFILEHEADER));

	bm = (char*)(&bmfh.bfType);
	if ( bm[0]!='B' || bm[1]!='M' ) goto error;



	read(bmpfile, &bmiHeader, sizeof(BITMAPINFOHEADER));

	if (bmiHeader.biBitCount		!= 8		||
		bmiHeader.biWidth			!= 256		||
		bmiHeader.biHeight			!= 128		||
		bmiHeader.biCompression		!= BI_RGB	||
		bmiHeader.biClrUsed			>  256
		) goto error;


	{
		int sz = bmiHeader.biClrUsed;
		if (sz == 0) sz=256;
		memset(rgbq, 0, sizeof(RGBQUAD)*256);
		read(bmpfile, rgbq, sizeof(RGBQUAD) * sz);
	}

	{
		char buf[32768];
		int x,y,i;
		int r,g,b;
		for(y=0; y<bmiHeader.biHeight; y++)
			read(bmpfile, &buf[(bmiHeader.biHeight-1-y) * ((bmiHeader.biWidth * (bmiHeader.biBitCount/8)+3)&~3)], (bmiHeader.biWidth * (bmiHeader.biBitCount/8)+3)&~3);

		i=0;
		for(r=0; r<32; r++)
		{
			for(g=0; g<32; g++)
			{
				for(b=0; b<32; b++)
				{
					x = (r + (b&0x7)*32);
					y = (g + (b>>3)*32);
					cnv[i++] = buf[x+y*256];
				}
			}
		}
	}

	close(bmpfile);
	return 0;
error:
	close(bmpfile);
	return -1;
}

#include "driver.h"
#include "options.h"
#ifdef MAME_AVI
//#include "options.h"
#include <stdio.h>
struct MAME_AVI_STATUS_SAVEDIR *avifile_dir_serch(char *str, struct MAME_AVI_STATUS_SAVEDIR *savedir)
{
	char *ch;
	ch = strchr(str,'#');
	if (ch != NULL)
	{
		ch++;
		if (savedir == NULL)
		{
			if (!memcmp(ch, "FILE ", 5) && savedir == NULL)
			{
				int count;
				char *ch2;
				ch+=5;
				ch2 = strchr(ch,0x0d);
				*ch2 = 0;
				sscanf(ch,"%u", &count);
				if (count <= 0) goto exit;

				savedir = malloc(sizeof(struct MAME_AVI_STATUS_SAVEDIR) * count);
				if (savedir == NULL) return NULL;
				memset(savedir,0,sizeof(struct MAME_AVI_STATUS_SAVEDIR) * count);

			}
		} else
		{
			if (!memcmp(ch, "AVI", 3))
			{
				int i,filenmb;
				char nmb[5];
				char *ch2;
				ch+=3;
				memcpy(nmb,ch,4);
				nmb[4] = '0';
				sscanf(nmb,"%u", &filenmb);

				ch+=4;

				ch = strchr(ch,'\"');
				if (ch == NULL) goto exit;
				ch++;

				ch2 = strchr(ch,'\"');
				if (ch2 == NULL) goto exit;

				memcpy(savedir[filenmb].filename, ch, (ch2-ch));
				savedir[filenmb].filename[(int)(ch2-ch)] = 0;

				ch = strchr(ch2,',');
				if (ch == NULL) goto exit;
				ch++;

				sscanf(ch,"%u,%u,%d", &savedir[filenmb].filesize, &savedir[filenmb].filesizecheck_frame, &i);
				if (i) savedir[filenmb].pause = TRUE;	else	savedir[filenmb].pause = FALSE;

			}
		}

	}

exit:
	return savedir;
}

struct MAME_AVI_STATUS_SAVEDIR *load_avifile_dir(void *filename)
{
	int file = -1;
	char str[1024];
	struct MAME_AVI_STATUS_SAVEDIR *savedir = NULL;
	int i,m;
	int filesize;

	file = open(filename, O_RDONLY | O_BINARY);
	if (file == -1) return NULL;

	filesize = filelength(file);
	if (filesize<=0) return NULL;

	while(filesize>0)
	{
	m=0;
	for (i=0; i<filesize; i++)
	{
		read(file, &str[m], 1);
		if ( str[m] == 0x0d) break;
		if ( str[m] == 0x0a) break;
		m++;
	}
	filesize -= i;

	if (m>1024) m=1024;
	str[m] = 0;

	savedir = avifile_dir_serch(str, savedir);
	}
	close(file);

	return savedir;
}
#endif	/* MAME_AVI */
#endif

/*-------------------------------------------------*/

struct KAILLERA_CHATDATA_PREPARATIONCHECK KailleraChatdataPreparationcheck;

void PreparationcheckClear(void)
{
	(*KailleraChatdataPreparationcheck.Callback)( 1 );

	PreparationcheckReset();
}

void PreparationcheckReset(void)
{
	KailleraChatdataPreparationcheck.nmb			= 0;
	KailleraChatdataPreparationcheck.str			= (char *)"";
	KailleraChatdataPreparationcheck.count			= 0;
	KailleraChatdataPreparationcheck.timeremainder	= 0;
	KailleraChatdataPreparationcheck.flag			= 0;
	KailleraChatdataPreparationcheck.Callback			= PreparationcheckNull;
	KailleraChatdataPreparationcheck.Callback_Start		= PreparationcheckNull;
	KailleraChatdataPreparationcheck.Callback_Update	= PreparationcheckNull_Update;
}

void __cdecl PreparationcheckNull(int flag)
{
	return;
}

void __cdecl PreparationcheckNull_Update(int flag, unsigned long *data)
{
	return;
}

void KailleraLostConnectionCheck(int command)
{
	static unsigned int time;
	if (!command)
	{
		time = timeGetTime();

	} else
	{
		const unsigned int tm = timeGetTime();
		if ( tm - time > KailleraStartOption.lost_connection_time &&
			KailleraStartOption.lost_connection_time)
		{
			if (tm>time && !(Kaillera_StatusFlag & KAILLERA_STATUSFLAG_LOST_CONNECTION))
			{
				if (KailleraStartOption.lost_connection_operation < KAILLERA_LOST_CONNECTION_OPERATION_END )
					popmessage( _String(_UIW(L"Disconnected, Because there was no response %ums")), KailleraStartOption.lost_connection_time );
				KailleraLostConnection();
			}
		}
	}
}

void KailleraLostConnection(void)
{
	if (!(Kaillera_StatusFlag & KAILLERA_STATUSFLAG_LOST_CONNECTION))
	{
		extern void KailleraChatEnd(void);
		long dat[64];

		Kaillera_StatusFlag |= KAILLERA_STATUSFLAG_LOST_CONNECTION;

		if (KailleraStartOption.lost_connection_time)
		{
			kailleraEndGame();
			switch (KailleraStartOption.lost_connection_operation)
			{
			case KAILLERA_LOST_CONNECTION_OPERATION_NONE:	default:
				break;
				
			case KAILLERA_LOST_CONNECTION_OPERATION_WINDOW_MODE:
				if (!win_window_mode)
					winwindow_toggle_full_screen();
				break;
				
			case KAILLERA_LOST_CONNECTION_OPERATION_END:
				quiting = 2;
				KailleraChatEnd();
				break;
				
			case KAILLERA_LOST_CONNECTION_OPERATION_END_ALL_PLAYERS:
				if (KailleraStartOption.player == 1)
				{
					dat[0] = 12;
					dat[1] = 0xffffffff;	//全員ゲーム終了
					kailleraChatSend(kChatData(&dat[0], 8));
				}
				
				quiting = 2;
				KailleraChatEnd();
				break;
			}
		}
	}

}
