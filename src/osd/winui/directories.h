/***************************************************************************

  M.A.M.E.UI  -  Multiple Arcade Machine Emulator with User Interface
  Win32 Portions Copyright (C) 1997-2003 Michael Soderstrom and Chris Kirmse,
  Copyright (C) 2003-2007 Chris Kirmse and the MAME32/MAMEUI team.

  This file is part of MAMEUI, and may only be used, modified and
  distributed under the terms of the MAME license, in "readme.txt".
  By continuing to use, modify or distribute this file you indicate
  that you have read the license and understand and accept it fully.

 ***************************************************************************/

#ifndef DIRECTORIES_H
#define DIRECTORIES_H

/* Dialog return codes */
#define DIRDLG_ROMS         0x0010
#define DIRDLG_SAMPLES      0x0020
#define DIRDLG_INI		    0x0040
#define DIRDLG_CFG          0x0100
#define DIRDLG_HI           0x0200
#define DIRDLG_IMG          0x0400
#define DIRDLG_INP          0x0800
#define DIRDLG_CTRLR        0x1000
#define DIRDLG_SOFTWARE		0x2000
#define DIRDLG_COMMENT      0x4000
#define DIRDLG_CHEAT        0x8000
#ifdef MAME_AVI
#define DIRDLG_AVI          0x8000
#endif /* MAME_AVI */

#define DIRLIST_NEWENTRYTEXT "<               >"

typedef struct
{
	LPCWSTR   lpName;
	LPCWSTR   (*pfnGetTheseDirs)(void);
	void     (*pfnSetTheseDirs)(LPCWSTR lpDirs);
	BOOL     bMulti;
	int      nDirDlgFlags;
}
DIRECTORYINFO;

/* in layout[ms].c */
extern const DIRECTORYINFO g_directoryInfo[];

INT_PTR CALLBACK DirectoriesDialogProc(HWND hDlg, UINT Msg, WPARAM wParam, LPARAM lParam);

#endif /* DIRECTORIES_H */


