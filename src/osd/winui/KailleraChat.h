/***************************************************************************

  M.A.M.E.32  -  Multiple Arcade Machine Emulator for Win32
  Win32 Portions Copyright (C) 1997-2001 Michael Soderstrom and Chris Kirmse

  This file is part of MAME32, and may only be used, modified and
  distributed under the terms of the MAME license, in "readme.txt".
  By continuing to use, modify or distribute this file you indicate
  that you have read the license and understand and accept it fully.

 ***************************************************************************/

#ifndef KAILLERACHAT_H
#define KAILLERACHAT_H

#ifndef BOOL
#include <windows.h>
#endif

void KailleraChatInit(running_machine &machine);
void KailleraChatExit(void);
void KailleraChatReInit(running_machine &machine);
void KailleraChatUpdate(running_machine *machine, render_container *container);
void KailleraChateReceive(char *szText);
int  KailleraChatIsActive(void);
unsigned char *KailleraChatGetStrAttr(void);
int *KailleraChatGetStrClause(void);
void KailleraChatEnd(void);

extern running_machine *k_machine;

#endif
