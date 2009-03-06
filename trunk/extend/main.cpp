/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "win32.h"

extern "C" BOOL APIENTRY DllMain(HANDLE,  DWORD, LPVOID);

BOOL APIENTRY
DllMain(HANDLE instance, DWORD reason, LPVOID)
{
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    win32::instance = HMODULE(instance);
    break;
  }
  return TRUE;
}
