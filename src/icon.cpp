/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#define RT_MASCOTICON "MASCOTICON"

/*
 * Functions of the class iconmodule
 */
iconmodule::iconmodule(std::string const& fn)
  : _rep(new rep(fn.empty() ? HMODULE(win32::exe) :
		 LoadLibraryEx(path(fn.c_str()).c_str(), {}, LOAD_LIBRARY_AS_DATAFILE)))
{}

iconmodule::iconmodule(iconmodule const& module) noexcept
  : _rep(module._rep)
{
  ++_rep->count;
}

iconmodule&
iconmodule::operator=(iconmodule const& module) noexcept
{
  ++module._rep->count;
  _release();
  _rep = module._rep;
  return *this;
}

void
iconmodule::_release()
{
  if (--_rep->count == 0) delete _rep;
}

std::string
iconmodule::path(LPCSTR fn)
{
  char path[MAX_PATH];
  win32::valid(GetModuleFileName({}, path, MAX_PATH));
  fn && *fn && win32::valid(PathRemoveFileSpec(path) && PathCombine(path, path, fn));
  return path;
}

void
iconmodule::collect(accept& accept) const noexcept
{
  if (!_rep->module) return;
  struct _enum {
    iconmodule const* module;
    iconmodule::accept* accept;
  } param { this, &accept };
  EnumResourceNames(_rep->module, RT_MASCOTICON,
		    [](auto, auto, auto name, auto param) {
		      try {
			int id = LOWORD(name);
			if (name != MAKEINTRESOURCE(id)) {
			  if (name[0] != '#') return TRUE;
			  id = StrToInt(name + 1);
			}
			auto& p = *reinterpret_cast<_enum*>(param);
			(*p.accept)(id, icon(id, *p.module));
		      } catch (...) {}
		      return TRUE;
		    }, LONG_PTR(&param));
}

/*
 * Functions of the class icon
 */
icon::icon(int id, iconmodule const& mod)
  : _mod(win32::valid(mod))
{
  HRSRC h = win32::valid(FindResource(_mod, MAKEINTRESOURCE(id), RT_MASCOTICON));
  DWORD rsz = SizeofResource(_mod, h);
  _rc = PWORD(win32::valid(LockResource(LoadResource(_mod, h))));
  if (rsz < 2 || (_rc[0] & 1) || rsz < _rc[0] || _rc[0] < 8 ||
      _rc[2] == 0 || rsz < _rc[0] + _rc[2] * sizeof(anim)) {
    throw std::runtime_error("Invalid icon resource.");
  }
  for (int t = 0; t < 3; ++t) {
    anim const* p = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]) + _rc[2] * t;
    for (int i = 0; i < _rc[2]; ++i) {
      win32::valid(FindResource(_mod, MAKEINTRESOURCE(p[i].id), RT_GROUP_ICON));
      if (p[i].ticks == 0) break;
    }
  }
  _size = size();
}

icon::~icon()
{
  _icon && DestroyIcon(_icon);
}

icon&
icon::operator=(icon const& copy) noexcept
{
  if (this != &copy) {
    _mod = copy._mod, _rc = copy._rc, _anim = {}, _size = copy._size, _step = 0;
    if (_icon) DestroyIcon(_icon), _icon = {};
  }
  return *this;
}

HICON
icon::_read(int id, int size) const noexcept
{
  if (size <= 0) size = _size;
  return HICON(LoadImage(_mod, MAKEINTRESOURCE(id),
			 IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

icon&
icon::_load(int step) noexcept
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
icon::resize(int size) noexcept
{
  assert(size > 0);
  if (_size != size) _size = size, _load();
  return *this;
}

icon&
icon::reset(int type) noexcept
{
  assert(type >= 0 && type <= 2);
  _anim = reinterpret_cast<anim*>(PBYTE(_rc) + _rc[0]) + _rc[2] * type;
  return _load();
}

icon&
icon::next() noexcept
{
  return _load((_step + 1) % _rc[2]);
}

UINT
icon::delay() const noexcept
{
  return _anim ? _anim[_step].ticks * 50 / 3 : 0;
}
