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

#define RT_MASCOTICON "MASCOTICON"

/*
 * Functions of the class iconmodule
 */
iconmodule::iconmodule(LPCSTR fn)
{
  string s = path(fn);
  _rep = new rep;
  _rep->module = s.empty() ? win32::exe :
    LoadLibraryEx(s.c_str(), NULL, LOAD_LIBRARY_AS_DATAFILE);
}

iconmodule::iconmodule(const iconmodule& module)
  : _rep(module._rep)
{
  ++_rep->count;
}

const iconmodule&
iconmodule::operator=(const iconmodule& module)
{
  module._rep->count++;
  _release();
  _rep = module._rep;
  return *this;
}

void
iconmodule::_release()
{
  if (--_rep->count) return;
  HMODULE h = _rep->module;
  delete _rep;
  h && h != win32::exe && FreeLibrary(h);
}

string
iconmodule::path(LPCSTR fn)
{
  char path[MAX_PATH] = "";
  if (fn && *fn) {
    win32::valid(GetModuleFileName(NULL, path, MAX_PATH) &&
		 PathRemoveFileSpec(path) && PathCombine(path, path, fn));
  }
  return path;
}

static BOOL CALLBACK
enumicon(HMODULE, LPCSTR, LPSTR name, LONG_PTR param)
{
  try {
    if (name != LPSTR(int(name) & 0xffff)) {
      if (name[0] != '#') return TRUE;
      name = MAKEINTRESOURCE(StrToInt(name + 1));
    }
    LONG_PTR* p = reinterpret_cast<LONG_PTR*>(param);
    (*reinterpret_cast<iconmodule::accept*>(p[1]))
      (name, icon(name, *reinterpret_cast<iconmodule*>(p[0])));
  } catch (...) {}
  return TRUE;
}

void
iconmodule::collect(accept& accept) const
{
  if (!_rep->module) return;
  LONG_PTR param[] = { reinterpret_cast<LONG_PTR>(this), LONG_PTR(&accept) };
  EnumResourceNames(_rep->module, RT_MASCOTICON, enumicon, LONG_PTR(param));
}

/*
 * Functions of the class icon
 */
icon::icon(LPCSTR id, const iconmodule& mod)
  : _mod(mod), _anim(NULL), _step(0), _icon(NULL)
{
  HRSRC h = win32::valid(FindResource(_mod, id, RT_MASCOTICON));
  DWORD rsz = SizeofResource(_mod, h);
  _rc = PWORD(win32::valid(LockResource(LoadResource(_mod, h))));
  if (rsz < 2 || (_rc[0] & 1) || rsz < _rc[0] || _rc[0] < 8 ||
      _rc[2] == 0 || rsz < _rc[0] + _rc[2] * sizeof(anim)) {
    throw runtime_error("Invalid icon resource.");
  }
  for (int t = 0; t < 3; ++t) {
    const anim* p = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]) + _rc[2] * t;
    for (int i = 0; i < _rc[2]; ++i) {
      win32::valid(FindResource(_mod, MAKEINTRESOURCE(p[i].id), RT_ICON));
      if (p[i].ticks == 0) break;
    }
  }
  _size = size();
}

icon::~icon()
{
  _icon && DestroyIcon(_icon);
}

const icon&
icon::operator=(const icon& copy)
{
  if (this != &copy) {
    _mod = copy._mod, _rc = copy._rc, _anim = copy._anim,
    _size = copy._size, _step = copy._step;
    if (_icon) DestroyIcon(_icon), _icon = NULL;
  }
  return *this;
}

HICON
icon::_read(int id, int size) const
{
  if (size <= 0) size = _size;
  return HICON(LoadImage(_mod, MAKEINTRESOURCE(id),
			 IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

icon&
icon::_load(int step)
{
  assert(step >= 0 && step < _rc[2]);
  HICON icon = _read(_anim ? _anim[_step = _anim[step].id ? step : 0].id : _rc[3]);
  if (icon) {
    if (_icon) DestroyIcon(_icon);
    _icon = icon;
  }
  return *this;
}

icon&
icon::resize(int size)
{
  assert(size > 0);
  if (_size != size) _size = size, _load();
  return *this;
}

icon&
icon::reset(int type)
{
  assert(type >= 0 && type <= 2);
  _anim = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]) + _rc[2] * type;
  return _load();
}

icon&
icon::next()
{
  return _load((_step + 1) % _rc[2]);
}

UINT
icon::delay() const
{
  return _anim ? _anim[_step].ticks * 50 / 3 : 0;
}
