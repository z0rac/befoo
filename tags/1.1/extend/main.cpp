/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "win32.h"

namespace extend { extern win32::module dll; }
win32::module extend::dll;

extern "C" BOOL APIENTRY DllMain(HANDLE,  DWORD, LPVOID);

BOOL APIENTRY
DllMain(HANDLE instance, DWORD reason, LPVOID)
{
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    extend::dll = HMODULE(instance);
    break;
  }
  return TRUE;
}
