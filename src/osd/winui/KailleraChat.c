/***************************************************************************

  M.A.M.E.32  -  Multiple Arcade Machine Emulator for Win32
  Win32 Portions Copyright (C) 1997-2001 Michael Soderstrom and Chris Kirmse

  This file is part of MAME32, and may only be used, modified and
  distributed under the terms of the MAME license, in "readme.txt".
  By continuing to use, modify or distribute this file you indicate
  that you have read the license and understand and accept it fully.

 ***************************************************************************/

 /***************************************************************************

  KailleraChat.c

  Fullscreen chat for Kaillera.

  Chat Display Functions Created by japmame.

  Japanese IMM Functions Created 15/5/2001  by NJ.

***************************************************************************/

#define WIN32_LEAN_AND_MEAN
#undef _UNICODE
#undef UNICODE
#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <tchar.h>
#include "emu.h"
#include "winui.h"
#include "resource.h"
#include "ui.h"
#include "kailleraclient.h"
#include "imm.h"
#include "uiinput.h"
#include "uilang.h"
#include "bitmask.h"
#include "mui_opts.h"
#include "strconv.h"
#include "translate.h"
#include "deprecat.h"

#include "window.h"
#define win_video_window		win_window_list->hwnd
#include "video.h"
#define win_window_mode video_config.windowed

/***************************************************************************
    Macros and definitions
 ***************************************************************************/

//#define FORCE_CLOSE_COMPOSITION_WINDOW 0

#define KC_BUFFER_MAX    256
#define KC_SCROLL_SPD    100
#define KC_BLINK_SPD     20

#define KC_CHATLINE_MAX  8
#define KC_EDITLINE      1
#define KC_CONVSTR       1
#define KC_CURSOR        1
#define KC_IME           1
#define KC_TERMINATE     1

#define DT_MAX           (KC_CHATLINE_MAX + KC_EDITLINE + KC_CONVSTR + KC_CURSOR + KC_IME + KC_TERMINATE)

#define IME_CLOSE         0
#define IME_OPEN          1
#define IME_CONVERSION    2

#define ISSJIS(c) (((c) >= 0x81 && (c) <= 0x9f) || ((c) >= 0xe0 && (c) <= 0xfc))

typedef BOOL (WINAPI *ime_proc)(HWND hWnd, BOOL bflag);

#ifdef KAILLERA
#define UI_COLOR_NORMAL			0		/* white on black text */
#define UI_COLOR_INVERSE		1		/* black on white text */
#define UI_COLOR_TRANS			0x02	/* white text only */
#define UI_COLOR_ATTRIBUTE		0x04	/* for IME */
#define UI_COLOR_BLEND			0x05	/* 半透明 Back  できなければ黒 */
#define UI_COLOR_BACKBLACK		0x06	/* white on black text */
#define UI_COLOR_MESH			0x07
#define UI_COLOR_SHADOW			0x08
#define UI_COLOR_FRAME			0x09
#define UI_COLOR_FRAME_2		0x0a
#define UI_COLOR_FRAME_3		0x0b

#define UI_COLOR_MASK			0x0f
#define UI_UNDERLINE			0x10
#define UI_UNDERLINE_BOLD		0x20
#define UI_UNDERLINE_DOT		0x40

#define UI_UNDERLINE_INVERSE      (UI_UNDERLINE | UI_COLOR_INVERSE)
#define UI_UNDERLINE_DOT_INVERSE  (UI_UNDERLINE_DOT | UI_COLOR_INVERSE)
#define UI_UNDERLINE_BOLD_INVERSE (UI_UNDERLINE_BOLD | UI_COLOR_INVERSE)
#endif /* KAILLERA */

/***************************************************************************
    Function prototypes
 ***************************************************************************/

static LRESULT CALLBACK EditProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
static void             KailleraChatCloseChat(void);
static void             KailleraChatImeGetConversionStatus(void);
static BOOL             MSIME2K_GetReg(void);
//static void             GetWinVersion(BOOL *bIsNT, int *nMajor, int *nMinor, int *nBuildNumber);

extern void             ui_draw_box(render_container *container, float x0, float y0, float x1, float y1, rgb_t backcolor);
extern void             displaychatlog(running_machine *machine, render_container *container, char *text);
extern HWND             GetGameWindow(void);
extern void             win_pause_input(running_machine *machine, int paused);

void KailleraChatLogClear(void);

/***************************************************************************
    External variables
 ***************************************************************************/

int nChatDrawMode;
static HWND	hChat		= NULL;
running_machine *k_machine;

/***************************************************************************
    Internal variables
 ***************************************************************************/

static BOOL            bChatActive;
static BOOL            bUseIME;
static BOOL            bInputEnable;
static BOOL            bShowLog;
static int             nIMEState;
static int             nPrevIMEState;
static int             nScroll;
static int             nIndex;

static int             nEditMaxLen;
static float           nAdjustHeight;
static int             nCursorPos;
static int             nBlinkCounter;
static BOOL            bShowCursor;

static HANDLE          hUser32DLL;

static char            szRecvBuf[KC_BUFFER_MAX * 4];
static char            szChatText[KC_CHATLINE_MAX][KC_BUFFER_MAX];
static char            szChatLog[8192];

static char            szInputBuf[KC_BUFFER_MAX];
static char            szInputBuf_tmp[KC_BUFFER_MAX];
static char            szConvStrBuf[KC_BUFFER_MAX];
static char            szConvAttrBuf[KC_BUFFER_MAX];
static int             pConvClauseBuf[KC_BUFFER_MAX];
static char            *szInputBuf_utf8 = NULL;
static char            *szConvStrBuf_utf8 = NULL;

static BOOL            bCloseCompositionWindow;
static int             nIMEType;
static TCHAR           szIMEName[64];
static char            szIMEStateStr[16];
static char            szIMEStateStrPrev[16];
static char            *szIMEStateStr_utf8[6];
static char            *szIMEStateStr_mb[6]={
(char *)"ｺｰﾄﾞ",
(char *)"あ",
(char *)"カ",
(char *)"ｶ ",
(char *)"Ａ",
(char *)"A "
};

static ime_proc        _WINNLSEnableIME;
static WNDPROC         DefaultWindowProc;

enum {
   MSIME_2000 = 0,
   MSIME_OLD,
   ATOK12,
   DEFAULT_IME,
   MAX_IME_TYPE
};

static char Cursor[MAX_IME_TYPE] = {
    '|', /* MS-IME 2000 */
    '_', /* MS-IME 95/97/98 */
    '_', /* ATOK12 */
    '_'  /* default */
};

static int Attribute[MAX_IME_TYPE][5] = {
    /* MS-IME 2000 */
    {
        UI_UNDERLINE_DOT,
        UI_UNDERLINE_BOLD,
        UI_UNDERLINE,
        UI_UNDERLINE_INVERSE,
        UI_UNDERLINE,
    },
    /* MS-IME 95/97/98 */
    {
        UI_UNDERLINE_DOT,
        UI_UNDERLINE_INVERSE,
        UI_UNDERLINE,
        UI_UNDERLINE_INVERSE,
        UI_UNDERLINE_DOT,
    },
    /* ATOK12 */
    {
        UI_UNDERLINE_DOT,
        UI_UNDERLINE_DOT_INVERSE,
        UI_UNDERLINE_DOT,
        UI_UNDERLINE_DOT_INVERSE,
        UI_UNDERLINE_DOT,
    },
    /* default */
    {
        UI_UNDERLINE_DOT,
        UI_UNDERLINE_INVERSE,
        UI_UNDERLINE,
        UI_UNDERLINE_INVERSE,
        UI_UNDERLINE_DOT,
    }
};

/***************************************************************************
    External functions
 ***************************************************************************/

void KailleraThrottleChange(int mode)
{
}

void KailleraChatInit(running_machine &machine)
{
    int  i;
    HIMC hImc;
    HKL  hKeyboardLayout;
    UINT error_mode;
	float space_width = ui_get_char_width(machine, 'W');

    bChatActive   = FALSE;
    bInputEnable  = FALSE;
    nPrevIMEState = IME_CLOSE;
    nIMEState     = IME_CLOSE;
    nIndex        = 0;
    nScroll       = 0;
    nChatDrawMode = GetChatDrawMode();
    bUseIME       = GetUseImeInChat();
    bShowLog      = FALSE;
	hChat         = win_video_window;

	for (i = 0; i < 6; i++)
		szIMEStateStr_utf8[i] = utf8_from_astring(szIMEStateStr_mb[i]);

    for (i = 0; i < KC_CHATLINE_MAX; i++)
        ZeroMemory(szChatText[i], KC_BUFFER_MAX);

    ZeroMemory(szRecvBuf, KC_BUFFER_MAX * 4);
    ZeroMemory(szInputBuf, KC_BUFFER_MAX);
    ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
    //ZeroMemory(szChatLog, 65536);	//kt del
    KailleraChatLogClear();

    hImc = ImmGetContext(hChat);

    if (!ImmGetOpenStatus(hImc))
    {
        ImmSetOpenStatus(hImc, TRUE);
    }

    hKeyboardLayout = GetKeyboardLayout(0);

    if (ImmIsIME(hKeyboardLayout))
    {
        ImmGetDescription(hKeyboardLayout, szIMEName, 64);

#if 1
        {
            FILE *fp;

            fp = fopen("imename.txt", "w");
            if (fp != NULL)
            {
                _ftprintf(fp, TEXT("%s\n"), szIMEName);
                fclose(fp);
            }
        }
#endif

        if (!_tcscmp(szIMEName, TEXT("Microsoft IME Standard 2003")))
        {
            nIMEType = MSIME_2000;
                bCloseCompositionWindow = FALSE;
#if 0
            if (MSIME2K_GetReg())
                bCloseCompositionWindow = FALSE;
            else
                bCloseCompositionWindow = TRUE;
#endif
        }
        else
        if (!_tcscmp(szIMEName, TEXT("Microsoft IME 2000 (Japanese)")))
        {
            nIMEType = MSIME_2000;
            if (MSIME2K_GetReg())
                bCloseCompositionWindow = FALSE;
            else
                bCloseCompositionWindow = TRUE;
        }
        else
#if 0
        if (!_tcscmp(szIMEName, TEXT("Microsoft IME 98 日本語入力システム")))
        {
            nIMEType = MSIME_OLD;
            bCloseCompositionWindow = FALSE;
        }
        else
#endif
        if (!_tcscmp(szIMEName, TEXT("ATOK12")))
        {
            nIMEType = ATOK12;
            bCloseCompositionWindow = FALSE;
        }
        else
        {
            nIMEType = DEFAULT_IME;
            bCloseCompositionWindow = FALSE;
        }

        /* WINNLSEnableIMEは、海外だと使用できないので、存在するかどうかチェック */
        /* 本当はImmDisableIME()を使用したいところだけど */
        error_mode = SetErrorMode(0);
        hUser32DLL = LoadLibrary(TEXT("user32.dll"));
        SetErrorMode(error_mode);

        if (hUser32DLL != NULL)
            _WINNLSEnableIME = (ime_proc)GetProcAddress((HINSTANCE)hUser32DLL, "WINNLSEnableIME");

        if (_WINNLSEnableIME == NULL)
        {
            FreeLibrary((HINSTANCE)hUser32DLL);
            hUser32DLL = NULL;
        }
    }
    else
    {
        bUseIME = FALSE;
        hUser32DLL = NULL;
        nIMEType = DEFAULT_IME;
        _WINNLSEnableIME = NULL;
        bCloseCompositionWindow = FALSE;
    }

    if (ImmGetOpenStatus(hImc))
        ImmSetOpenStatus(hImc, FALSE);

    ImmReleaseContext(hChat, hImc);

    if (bUseIME)
    {
        KailleraChatImeGetConversionStatus();
        nEditMaxLen   = (int)((1.0f - (float)(5 + strlen(szIMEStateStr)) * space_width) / space_width) - 1;
        nAdjustHeight = 0; //UI_LINE_WIDTH * 3; /* IME用に3ドット余分に確保 */
        szIMEStateStrPrev[0]=0;
    }
    else
    {
        nEditMaxLen   = (int)((1.0f - 5.0f * space_width) / space_width) - 1;
        nAdjustHeight = 0;
    }

    if (_WINNLSEnableIME)
        _WINNLSEnableIME(hChat, FALSE);

    DefaultWindowProc = (WNDPROC)(LONG)(int)GetWindowLong(hChat, GWL_WNDPROC);
    SetWindowLong(hChat, GWL_WNDPROC, (LONG)EditProc);
}


void KailleraChatExit(void)
{
	int i;

	if(hChat) {
		if(DefaultWindowProc) {
			SetWindowLongPtr(hChat, GWL_WNDPROC, (LONG)DefaultWindowProc);
			DefaultWindowProc = NULL;
		}

		hChat = NULL;
	}

	for (i = 0; i < 6; i++)
		if (szIMEStateStr_utf8[i])
		{
			free(szIMEStateStr_utf8[i]);
			szIMEStateStr_utf8[i] = NULL;
		}

    if (_WINNLSEnableIME)
        _WINNLSEnableIME(hChat, TRUE);

    if (hUser32DLL)
    {
        FreeLibrary((HINSTANCE)hUser32DLL);
        hUser32DLL = NULL;
    }
}

void KailleraChatReInit(running_machine &machine)
{
    HIMC hImc;
    HKL  hKeyboardLayout;
    UINT error_mode;
	float space_width = ui_get_char_width(machine, 'W');


    bChatActive   = FALSE;
    bInputEnable  = FALSE;
    nPrevIMEState = IME_CLOSE;
    nIMEState     = IME_CLOSE;
    nIndex        = 0;
    nScroll       = 0;

    hImc = ImmGetContext(hChat);

    if (!ImmGetOpenStatus(hImc))
    {
        ImmSetOpenStatus(hImc, TRUE);
    }

    hKeyboardLayout = GetKeyboardLayout(0);

    if (ImmIsIME(hKeyboardLayout))
    {
        ImmGetDescription(hKeyboardLayout, szIMEName, 64/2);
#if 0
        {
            FILE *fp;

            fp = fopen("imename.txt", "w");
            if (fp != NULL)
            {
                _ftprintf(fp, TEXT("%s\n"), szIMEName);
                fclose(fp);
            }
        }
#endif

        if (!_tcscmp(szIMEName, TEXT("Microsoft IME Standard 2003")))
        {
            nIMEType = MSIME_2000;
                bCloseCompositionWindow = FALSE;
#if 0
            if (MSIME2K_GetReg())
                bCloseCompositionWindow = FALSE;
            else
                bCloseCompositionWindow = TRUE;
#endif
        }
        else
        if (!_tcscmp(szIMEName, TEXT("Microsoft IME 2000 (Japanese)")))
        {
            nIMEType = MSIME_2000;
            if (MSIME2K_GetReg())
                bCloseCompositionWindow = FALSE;
            else
                bCloseCompositionWindow = TRUE;
        }
        else
#if 0
        if (!_tcscmp(szIMEName, TEXT("Microsoft IME 98 日本語入力システム")))
        {
            nIMEType = MSIME_OLD;
            bCloseCompositionWindow = FALSE;
        }
        else
#endif
        if (!_tcscmp(szIMEName, TEXT("ATOK12")))
        {
            nIMEType = ATOK12;
            bCloseCompositionWindow = FALSE;
        }
        else
        {
            nIMEType = DEFAULT_IME;
            bCloseCompositionWindow = FALSE;
        }

        /* WINNLSEnableIMEは、海外だと使用できないので、存在するかどうかチェック */
        /* 本当はImmDisableIME()を使用したいところだけど */
        error_mode = SetErrorMode(0);
        hUser32DLL = LoadLibrary(TEXT("user32.dll"));
        SetErrorMode(error_mode);

        if (hUser32DLL != NULL)
            _WINNLSEnableIME = (ime_proc)GetProcAddress((HINSTANCE)hUser32DLL, "WINNLSEnableIME");

        if (_WINNLSEnableIME == NULL)
        {
            FreeLibrary((HINSTANCE)hUser32DLL);
            hUser32DLL = NULL;
        }
    }
    else
    {
        bUseIME = FALSE;
        hUser32DLL = NULL;
        nIMEType = DEFAULT_IME;
        _WINNLSEnableIME = NULL;
        bCloseCompositionWindow = FALSE;
    }

    if (ImmGetOpenStatus(hImc))
        ImmSetOpenStatus(hImc, FALSE);

    ImmReleaseContext(hChat, hImc);

    if (bUseIME)
    {
        KailleraChatImeGetConversionStatus();
        nEditMaxLen   = (int)((1.0f - (float)(5 + strlen(szIMEStateStr)) * space_width) / space_width) - 1;
        nAdjustHeight = 0; //UI_LINE_WIDTH * 3; /* IME用に3ドット余分に確保 */
        szIMEStateStrPrev[0]=0;
    }
    else
    {
        nEditMaxLen   = (int)((1.0f - 5.0f * space_width) / space_width) - 1;
        nAdjustHeight = 0;
    }

    if (_WINNLSEnableIME)
        _WINNLSEnableIME(hChat, FALSE);

    DefaultWindowProc = (WNDPROC)(LONG)(int)GetWindowLong(hChat, GWL_WNDPROC);
    SetWindowLong(hChat, GWL_WNDPROC, (LONG)EditProc);
}

void KailleraChatUpdate(running_machine *machine, render_container *container)
{
    char szChatLine[KC_BUFFER_MAX + 6];
    char szCursor[4];
    int i;
    float j;
	float line_height = ui_get_line_height(*machine);
	float totalheight;
	char *utf8_string;

    if (bCloseCompositionWindow)
    {
        if (bUseIME && bChatActive)
        {
            /* IMEのデフォルトのウインドウがあったら有無を言わさず閉じる */
            HWND hIME = ImmGetDefaultIMEWnd(hChat);
            if (hIME != NULL)
                SendMessage(hIME, WM_CLOSE, 0, 0);
        }
    }

    /* keyboard input */
    if (!bChatActive)
    {
        if (ui_input_pressed(machine, IPT_UI_CHAT_OPEN))
        {
			input_device_class_enable(k_machine, DEVICE_CLASS_KEYBOARD, FALSE);
            if (bUseIME)
            {
                if (_WINNLSEnableIME)
                    _WINNLSEnableIME(hChat, TRUE);

                if (nPrevIMEState == IME_OPEN)
                {
                    HIMC hImc = ImmGetContext(hChat);
                    ImmSetOpenStatus(hImc, TRUE);
                    ImmReleaseContext(hChat, hImc);
                    nIMEState = IME_OPEN;
                }
                else
                    nIMEState = IME_CLOSE;
            }

            ZeroMemory(szInputBuf, KC_BUFFER_MAX);
            ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
            if (szIMEStateStrPrev[0])
                    strcpy(szIMEStateStr, szIMEStateStrPrev);

			if (szInputBuf_utf8)
			{
				free(szInputBuf_utf8);
				szInputBuf_utf8 = NULL;
			}
			if (szConvStrBuf_utf8)
			{
				free(szConvStrBuf_utf8);
				szConvStrBuf_utf8 = NULL;
			}

            nCursorPos   = 0;
            bInputEnable = TRUE;
            bChatActive  = TRUE;

        }
    }

    /* copy szRecvBuf to szChatText if line available */
    if (szRecvBuf[0])
    {
        strcpy(szChatText[nIndex], szRecvBuf);
        nIndex++;
        nIndex &= KC_CHATLINE_MAX - 1;
        szChatText[nIndex][0] = '\0';
        szRecvBuf[0] = '\0';
        nScroll = 0;
    }

    /* auto scroll out */
    nScroll++;
    if (nScroll > KC_SCROLL_SPD)
    {
        nScroll = 0;
        nIndex++;
        nIndex &= KC_CHATLINE_MAX - 1;
        szChatText[nIndex][0] = '\0';
    }

    /* display chat text */
    j = 0;
    for (i = 0; i < KC_CHATLINE_MAX; i++)
    {
        ui_draw_chattext(container, szChatText[(nIndex + i) % KC_CHATLINE_MAX], 0, j, nChatDrawMode, &totalheight);
        if (szChatText[(nIndex + i) % KC_CHATLINE_MAX][0])
        {
			if (totalheight > line_height)
				j += totalheight;
			else
				j += line_height;
        }
    }

	i = 0;

    /* display input buffer */
    if (bChatActive)
    {
        float x, y, x2, y2, cx;
        int nCursorDispPos;
        int nCursorColor;

        x  = 0;
        y  = 0 + (1.0f - line_height) - nAdjustHeight;
        x2 = 1.0f;
        y2 = 1.0f - nAdjustHeight;
		ui_draw_box(container, x, y, x2, y2,UI_BACKGROUND_COLOR);

        if (strlen(szInputBuf) > nEditMaxLen)
        {
            int nLimit;

            bInputEnable = FALSE;
            if (ISSJIS((BYTE)szInputBuf[nEditMaxLen - 1]))
                nLimit = nEditMaxLen - 1;
            else
                nLimit = nEditMaxLen;

            szInputBuf[nLimit] = '\0';
            if (nCursorPos > nLimit)
                nCursorPos = nLimit;
        }

		strcpy(szChatLine, "Chat:");
		if (szInputBuf_utf8)
			strcat(szChatLine, szInputBuf_utf8);

		ui_draw_colortext(container, szChatLine, 0, (1.0f - line_height) - nAdjustHeight, ARGB_WHITE);

        /* cursor blink */
        nBlinkCounter++;
        if (nBlinkCounter > KC_BLINK_SPD)
        {
            nBlinkCounter = 0;
            if (bShowCursor)
                bShowCursor = FALSE;
            else
                bShowCursor = TRUE;
        }

        ZeroMemory(szCursor, 4);
        nCursorDispPos = 5 + nCursorPos;
        nCursorColor   = bShowCursor ? ARGB_WHITE : UI_BACKGROUND_COLOR;
		cx = ui_get_string_width(*machine, "Chat:");
        if (szInputBuf[nCursorPos])
        {
            char *p = &szInputBuf[nCursorPos];

			strncpy(szInputBuf_tmp, szInputBuf, nCursorPos);
			szInputBuf_tmp[nCursorPos] = 0;
            utf8_string = utf8_from_astring(szInputBuf_tmp);
            if (utf8_string)
            {
                cx += ui_get_string_width(*machine, utf8_string);
                free(utf8_string);
            }

            szCursor[0] = *p;
            if (ISSJIS(*(BYTE *)p))
                szCursor[1] = *(p + 1);
            utf8_string = utf8_from_astring(szCursor);
            if (utf8_string)
            {
                strcpy(szCursor, utf8_string);
                free(utf8_string);
            } else strcpy(szCursor, " ");
        }
        else
        {
            szCursor[0] = ' ';
			if (szInputBuf_utf8) cx += ui_get_string_width(*machine, szInputBuf_utf8);
        }

        if (bUseIME)
        {
            if (szIMEStateStr[0])
            {
                ui_draw_colortext(container, szIMEStateStr, 1.0f - ui_get_string_width(*machine, szIMEStateStr_utf8[0]), 1.0f - line_height - nAdjustHeight, ARGB_WHITE);
            }

            if (nIMEState & IME_CONVERSION)
            {
                if (szConvStrBuf[0] == '\0')
                {
                    nIMEState &= ~IME_CONVERSION;
                }
                else
                {
                    int nStartPos;
                    int nMaxLen;
                    int nStrLen;

                    nStartPos = nCursorDispPos;
                    nStrLen   = strlen(szConvStrBuf);
                    nMaxLen   = 5 + nEditMaxLen;

                    while (nStartPos + nStrLen > nMaxLen)
                        nStartPos--;

					if (szConvStrBuf_utf8)
					{
						ui_draw_text(container, szConvStrBuf_utf8, cx, 1.0f - line_height - nAdjustHeight);
						cx += ui_get_string_width(*machine, szConvStrBuf_utf8);
					}

                    szCursor[0]    = Cursor[nIMEType];
                    szCursor[1]    = '\0';
                    nCursorDispPos = nStartPos + nStrLen;
                    nCursorColor   = RGB_WHITE;
                }
            }
        }

		ui_draw_text2(container, szCursor, cx, 1.0f - line_height - nAdjustHeight, nCursorColor);
    }
    else if (ui_input_pressed(machine, IPT_UI_CHAT_CHANGE_MODE))
    {
        nChatDrawMode++;
        if (nChatDrawMode >= 7)
            nChatDrawMode = 0;

        SetChatDrawMode(nChatDrawMode);
    }
    else if (ui_input_pressed(machine, IPT_UI_CHAT_SHOW_LOG))
    {
        bShowLog = !bShowLog;
        displaychatlog(machine, container, szChatLog);
    }

    if (bShowLog)
        displaychatlog(machine, container, NULL);
}

void KailleraChateReceive(char *szText)
{
	char *utf8_string;

	utf8_string = utf8_from_astring(szText);
	if (utf8_string)
	{
	    if (szRecvBuf[0])
	        strcat(szRecvBuf,"\n");
	    strcat(szRecvBuf, utf8_string);
	
		if( strlen(szChatLog) < (8192-512))
		{
			strcat(szChatLog, utf8_string);
		    strcat(szChatLog,"\n");
		}
		free(utf8_string);
	}
}

void KailleraChatLogClear(void)
{
	char *utf8_string;

	ZeroMemory(szChatLog, 8192);
	utf8_string = utf8_from_wstring(_UIW(L"\t- CHAT LOG -\n"));
	if (utf8_string)
	{
		strcat(szChatLog, utf8_string);
		free(utf8_string);
	}

	bShowLog      = FALSE;
}

int KailleraChatIsActive(void)
{
    return bChatActive;
}


unsigned char *KailleraChatGetStrAttr(void)
{
    return (unsigned char *)szConvAttrBuf;
}


int *KailleraChatGetStrClause(void)
{
    return pConvClauseBuf;
}


/***************************************************************************
    Internal functions
 ***************************************************************************/

static LRESULT CALLBACK EditProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    /* チャットアクティブ時のみ、MAME32のウインドウをフック */
    if (bChatActive)
    {
        HIMC  hImc;

        switch (Msg)
        {
		case WM_ACTIVATE:
            switch (wParam)
            {
			case WA_INACTIVE:
				KailleraChatCloseChat();
				return TRUE;
			}
			break;
        case WM_CHAR:
            switch (wParam)
            {
            case VK_RETURN:
            case VK_ESCAPE:
                return TRUE; /* disable beep */

            default:
                if (bChatActive && bInputEnable)
                {
                    int i, nLen;
                    char *p = &szInputBuf[nCursorPos];

                    nLen = strlen(szInputBuf);

                    if ((wParam & 0xff) > 0x1f)
                    {
                        if (strlen(szInputBuf) < KC_BUFFER_MAX - 1)
                        {
                            for (i = nLen; i >= nCursorPos; i--)
                                szInputBuf[i + 1] = szInputBuf[i];

                            *p = (unsigned char)wParam;
                            nCursorPos++;
                        }
                    }
					if (szInputBuf_utf8)
					{
						free(szInputBuf_utf8);
					}
					szInputBuf_utf8 = utf8_from_astring(szInputBuf);
                }
            }
            break;

        case WM_KEYDOWN:
            if (!(nIMEState & IME_CONVERSION))
            {
                char *p = &szInputBuf[nCursorPos];

                switch (wParam)
                {
                case VK_RETURN:
					ui_input_pressed(k_machine, IPT_UI_SELECT); /* IPT_UI_SELECTの押下フラグをクリア */
                    if (szInputBuf[0] == '\0')
					{
						KailleraChatCloseChat(); //kt
                        return TRUE;
					}
                    kailleraChatSend(szInputBuf);
                    KailleraChatCloseChat();
                    return TRUE;

                case VK_ESCAPE:
                    KailleraChatCloseChat();
                    ui_input_pressed(k_machine, IPT_UI_CANCEL); /* IPT_UI_CANCELの押下フラグをクリア */
                    return TRUE;
                
				case VK_HOME:
                    KailleraChatCloseChat(); //kt
		           return TRUE;

                case VK_BACK:
                    if (nCursorPos >= 2)
                    {
                        if (ISSJIS(*(BYTE *)(p - 2)))
                        {
                            while (*(p - 1))
                            {
                                *(p - 2) = *p;
                                p++;
                            }
                            nCursorPos -= 2;
                            bInputEnable = TRUE;
                            return TRUE;
                        }
                    }
                    if (nCursorPos != 0)
                    {
                        while (*(p - 1))
                        {
                            *(p - 1) = *p;
                            p++;
                        }
                        nCursorPos--;
                        bInputEnable = TRUE;
                        return TRUE;
                    }
                    break;

                case VK_LEFT:
                    if (nCursorPos >= 2)
                    {
                        if (ISSJIS(*(BYTE *)(p - 2)))
                        {
                            nCursorPos -= 2;
                            return TRUE;
                        }
                    }
                    if (nCursorPos != 0)
                    {
                        nCursorPos--;
                        return TRUE;
                    }
                    break;

                case VK_RIGHT:
                    {
                        int len = strlen(szInputBuf);

                        if (nCursorPos < len)
                        {
                            if (ISSJIS(*(BYTE *)p))
                                nCursorPos++;
                            nCursorPos++;
                            return TRUE;
                        }
                    }
                    break;

                case VK_DELETE:
                    if (ISSJIS(*(BYTE *)p))
                    {
                        while (*p)
                        {
                            *p = *(p + 2);
                            p++;
                        }
                        bInputEnable = TRUE;
                        return TRUE;
                    }
                    while (*p)
                    {
                        *p = *(p + 1);
                        p++;
                    }
                    bInputEnable = TRUE;
                    return TRUE;
                }
                break;
            }
            break;

        case WM_IME_STARTCOMPOSITION:
            if (!bCloseCompositionWindow)
            {
                COMPOSITIONFORM CompForm;

                hImc = ImmGetContext(hWnd);
                ImmGetCompositionWindow(hImc, &CompForm);
                CompForm.dwStyle = CFS_FORCE_POSITION;
                CompForm.ptCurrentPos.x = 65536;
                CompForm.ptCurrentPos.y = 65536;
                ImmSetCompositionWindow(hImc, &CompForm);
                ImmReleaseContext(hWnd, hImc);
            }
            break;

        case WM_IME_COMPOSITION:
            hImc = ImmGetContext(hWnd);
            nIMEState |= IME_CONVERSION;

            if (lParam & GCS_RESULTSTR)
            {
                /*
                 変換結果の取得
                 変換結果が返りますが、UOMAME32jではWM_CHARで拾っている
                 ので、変換が終わったかどうかの判断のみに使用しています。
                 使用するのであれば、以下のような感じでしょうか。

                 unsigned char szTemp[KC_BUFFER_MAX];

                 ZeroMemory(szTemp, KC_BUFFER_MAX);
                 ImmGetCompositionString(hImc, GCS_RESULTSTR, szConvStrBuf, KC_BUFFER_MAX - 1);
                 strcat(szInputBuf, szTemp);
                */

                ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
				if (szConvStrBuf_utf8)
				{
					free(szConvStrBuf_utf8);
					szConvStrBuf_utf8 = NULL;
				}
            }
            if (lParam & GCS_COMPSTR)
            {
                /*
                 変換中の文章の取得
                 変換中の文章そのものが返ります。
                */

                ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
				if (szConvStrBuf_utf8)
				{
					free(szConvStrBuf_utf8);
				}
                ImmGetCompositionString(hImc, GCS_COMPSTR, szConvStrBuf, KC_BUFFER_MAX - 1);
                szConvStrBuf_utf8 = utf8_from_astring(szConvStrBuf);
            }
            if (lParam & GCS_COMPATTR)
            {
                int i, len;
                char *p1, *p2;
                char szTemp[KC_BUFFER_MAX];

                /*
                 文字属性の取得
                 取得したデータは 以下のような感じだと思う
                  0x00 : 未確定文字         MS-IME2000では破線
                  0x01 : 変換対象の文節     MS-IME2000では太い下線
                  0x02 : 未確定の文節       MS-IME2000では下線
                  0x03 : 変換範囲の変更部分 MS-IME2000では反転
                  0x04 : 確定した文節       MS-IME2000では下線
                */
                len = strlen(szConvStrBuf);
                ImmGetCompositionString(hImc, GCS_COMPATTR, szTemp, len);
                p1  = szTemp;
                p2  = szConvAttrBuf;

                for (i = 0; i < len; i++)
                {
                    switch (*p1)
                    {
                    case 0x00:
                    case 0x01:
                    case 0x02:
                    case 0x03:
                    case 0x04:
                        *p2++ = Attribute[nIMEType][*(BYTE *)p1];
                        break;
                    }
                    p1++;
                }
            }
            if (lParam & GCS_COMPCLAUSE)
            {
                int i, j, len;
                int pTemp[KC_BUFFER_MAX];

                /* 
                 文節情報の取得
                 取得したデータは たとえば、今日は|良い|天気|です であれば
                 00000006|0000000a|0000000d|000000012 のように、32bit値で
                 文節の終わりの位置が入っています。
                 ゲームでは無理して取得する必要はないかも。
                */
                len = strlen(szConvStrBuf);
                ImmGetCompositionString(hImc, GCS_COMPCLAUSE, (unsigned char *)&pTemp, len * sizeof(int));

                for (i = 0, j = 0; i < len; i++)
                    if (pTemp[i])
                        pConvClauseBuf[j++] = pTemp[i];
            }
            ImmReleaseContext(hWnd, hImc);
            break;

        case WM_IME_ENDCOMPOSITION:
            /* コンポジションウィンドウが閉じられた */
            ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
			if (szConvStrBuf_utf8)
			{
				free(szConvStrBuf_utf8);
				szConvStrBuf_utf8 = NULL;
			}
            break;

        case WM_IME_NOTIFY:
            switch (wParam)
            {
            case IMN_SETOPENSTATUS:     /* IME オン/オフ */
                hImc = ImmGetContext(hWnd);
                if (ImmGetOpenStatus(hImc))
                    nIMEState |= IME_OPEN;
                else
                    nIMEState = IME_CLOSE;
                ImmReleaseContext(hWnd, hImc); /* breakしないで、入力モードも更新 */

            case IMN_SETCONVERSIONMODE: /* 入力モード変更     */
            case IMN_SETSENTENCEMODE:   /* 文節変換モード変更 */
                 KailleraChatImeGetConversionStatus();
                 break;
            }
            break;
        }
    }

    return CallWindowProc(DefaultWindowProc, hWnd, Msg, wParam, lParam);
}


static void KailleraChatCloseChat(void)
{
    if (bUseIME && !(nIMEState & IME_CONVERSION))
    {
        nPrevIMEState = nIMEState;

        if (nIMEState != IME_CLOSE)
        {
            HIMC hImc = ImmGetContext(hChat);
            ImmSetOpenStatus(hImc, FALSE);
            ImmReleaseContext(hChat, hImc);
        }
        nIMEState = IME_CLOSE;
    }

    if (_WINNLSEnableIME)
        _WINNLSEnableIME(hChat, FALSE);

    ZeroMemory(szInputBuf, KC_BUFFER_MAX);
    ZeroMemory(szConvStrBuf, KC_BUFFER_MAX);
    bInputEnable = FALSE;
    bChatActive  = FALSE;

	if (szInputBuf_utf8)
	{
		free(szInputBuf_utf8);
		szInputBuf_utf8 = NULL;
	}
	if (szConvStrBuf_utf8)
	{
		free(szConvStrBuf_utf8);
		szConvStrBuf_utf8 = NULL;
	}

	input_device_class_enable(k_machine, DEVICE_CLASS_KEYBOARD, TRUE);
}

void KailleraChatEnd(void)
{
	if ( bChatActive==TRUE )
		KailleraChatCloseChat();
}

static void KailleraChatImeGetConversionStatus(void)
{
    HIMC  hImc;
    BOOL  bOpen;
    DWORD dwConversion = 0;
    DWORD dwSentence;

    szIMEStateStr[0] = '\0';

    hImc = ImmGetContext(hChat);
    bOpen = ImmGetOpenStatus(hImc);
    if (bOpen)
        ImmGetConversionStatus(hImc, &dwConversion, &dwSentence);
    ImmReleaseContext(hChat, hImc);

    if (bOpen == TRUE)
    {

        /* 入力コード */
        switch (dwConversion & 0x000f)
        {
        case IME_CMODE_CHARCODE:
            //strcat(szIMEStateStr, "ｺｰﾄﾞ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[0]);
            break;

        case IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE:
            //strcat(szIMEStateStr, "あ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[1]);
            break;

        case IME_CMODE_NATIVE | IME_CMODE_FULLSHAPE | IME_CMODE_KATAKANA:
            //strcat(szIMEStateStr, "カ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[2]);
            break;

        case IME_CMODE_NATIVE | IME_CMODE_KATAKANA:
            //strcat(szIMEStateStr, "ｶ ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[3]);
            break;

        case IME_CMODE_FULLSHAPE:
            //strcat(szIMEStateStr, "Ａ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[4]);
            break;

        default:
            //strcat(szIMEStateStr, "A ");
            strcpy(szIMEStateStr, szIMEStateStr_utf8[5]);
            break;
        }

    }
    else
    {
        //strcat(szIMEStateStr, "A ");
        strcpy(szIMEStateStr, szIMEStateStr_utf8[5]);
    }
	strcpy(szIMEStateStrPrev, szIMEStateStr);
}

static BOOL MSIME2K_GetReg(void)
{
    HKEY  hKey;
    LONG  lResult;

    lResult = RegOpenKeyEx(HKEY_CURRENT_USER,
                           TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Ime\\Japan\\IMEJP\\MSIME"),
                           0, KEY_QUERY_VALUE, &hKey);

    if (lResult == ERROR_SUCCESS)
    {
        DWORD dwType;
        DWORD dwSize;
        DWORD dwValue;

        dwType = REG_DWORD;
        dwSize = 4;
        RegQueryValueEx(hKey, TEXT("HideComment"), 0, &dwType, (LPBYTE)&dwValue, &dwSize);
        RegCloseKey(hKey);
        return (dwValue != 0);
    }
    return FALSE;
}
