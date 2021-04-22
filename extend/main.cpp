/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "settingdlg.h"

win32::module extend::dll;

extern "C" BOOL APIENTRY
DllMain(HANDLE instance, DWORD reason, LPVOID)
{
  switch (reason) {
  case DLL_PROCESS_ATTACH:
    extend::dll = HMODULE(instance);
    break;
  }
  return TRUE;
}

extern "C" __declspec(dllexport) void
settingdlg(HWND hwnd, HINSTANCE, LPSTR cmdln, int)
{
  CoInitialize({});
  try {
    INITCOMMONCONTROLSEX icce { sizeof(icce), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icce);
    profile rep(cmdln);
    maindlg().modal(IDD_SETTING, hwnd);
  } catch (...) {}
  CoUninitialize();
}
