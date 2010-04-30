/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "icon.h"
#include <cassert>
#include <stdexcept>
#include <shlwapi.h>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of the class icon
 */
icon::icon(LPCSTR id, LPCSTR fn)
  : _icon(NULL)
{
  try {
    if (fn && *fn) {
      char path[MAX_PATH];
      if (GetModuleFileName(NULL, path, MAX_PATH) < MAX_PATH &&
	  PathRemoveFileSpec(path) && PathCombine(path, path, fn)) {
	_mod = win32::valid(LoadLibraryEx(path, NULL, LOAD_LIBRARY_AS_DATAFILE));
      }
    }
    HRSRC h = win32::valid(FindResource(_mod, id, RT_RCDATA));
    DWORD rsz = SizeofResource(_mod, h);
    _rc = PWORD(win32::valid(LockResource(LoadResource(_mod, h))));
    if (rsz < 2 || (_rc[0] & 1) || rsz < _rc[0] || _rc[0] < 8 ||
	_rc[2] == 0 || rsz < _rc[0] + _rc[2] * sizeof(anim)) {
      throw runtime_error("Invalid icon resource.");
    }
    _anim = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]);
    for (int t = 0; t < 3; ++t) {
      const anim* p = _anim + _rc[2] * t;
      for (int i = 0; i < _rc[2]; ++i) {
	win32::valid(FindResource(_mod, MAKEINTRESOURCE(p[i].id), RT_ICON));
	if (p[i].ticks == 0) break;
      }
    }
    _size = size(), _step = 0, _icon = win32::valid(_read(_rc[3]));
  } catch (...) {
    _release();
    throw;
  }
}

bool
icon::reload(LPCSTR id, LPCSTR fn)
{
  try {
    icon newicon(id, fn);
    _release();
    *this = newicon;
    newicon._mod = NULL, newicon._icon = NULL;
    return true;
  } catch (...) {
    return false;
  }
}

void
icon::_release()
{
  if (_icon) DestroyIcon(_icon);
  if (_mod) FreeLibrary(_mod);
}

HICON
icon::_read(int id)
{
  return HICON(LoadImage(_mod ? _mod : win32::exe, MAKEINTRESOURCE(id),
			 IMAGE_ICON, _size, _size, LR_DEFAULTCOLOR));
}

void
icon::_load(int step)
{
  assert(step >= 0 && step < _rc[2]);
  _step = _anim[step].id ? step : 0;
  HICON icon = _read(_anim[_step].id);
  if (icon) {
    if (_icon) DestroyIcon(_icon);
    _icon = icon;
  }
}

icon&
icon::resize(int size)
{
  assert(size > 0);
  if (_size != size) {
    _size = size;
    _load();
  }
  return *this;
}

icon&
icon::reset(int type)
{
  assert(type >= 0 && type <= 2);
  _anim = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]) + _rc[2] * type;
  _load();
  return *this;
}

HICON
icon::symbol(int size) const
{
  if (!size) size = _rc[1];
  return HICON(win32::valid(LoadImage(_mod ? _mod : win32::exe,
				      MAKEINTRESOURCE(_rc[3]), IMAGE_ICON,
				      size, size, LR_DEFAULTCOLOR)));
}
