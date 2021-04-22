/* -*- mode: c++ -*-
 * Copyright (C) 2010-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include "win32.h"

/** iconmodule - module includes animation icons
 */
class icon;
class iconmodule {
  struct rep {
    HMODULE module;
    size_t count = 1;
    rep(HMODULE module) : module(module) {}
    ~rep() { module && module != win32::exe && FreeLibrary(module); }
  };
  rep* _rep;
  void _release();
public:
  iconmodule(std::string const& fn = {});
  iconmodule(iconmodule const& module) noexcept;
  ~iconmodule() { _release(); }
  iconmodule& operator=(iconmodule const& module) noexcept;
  operator HMODULE() const noexcept { return _rep->module; }
  static std::string path(LPCSTR fn = {});
public:
  struct accept {
    virtual void operator()(int id, icon const& icon) = 0;
  };
  void collect(accept& accept) const noexcept;
};

/** icon - animation icons
 */
class icon {
  iconmodule _mod;
  PWORD _rc;
  struct anim { WORD id, ticks; };
  anim const* _anim = {};
  int _size;
  int _step = 0;
  HICON _icon = {};
  HICON _read(int id, int size = 0) const noexcept;
  icon& _load(int step = 0) noexcept;
public:
  icon(int id, iconmodule const& mod = {});
  icon(icon const& copy) noexcept { *this = copy; }
  ~icon();
  icon& operator=(icon const& copy) noexcept;
  operator HICON() const noexcept { return _icon; }
  int size() const noexcept { return _rc[1]; }
  icon& resize(int size) noexcept;
  icon& reset() noexcept { return _load(_step); }
  icon& reset(int type) noexcept;
  icon& next() noexcept;
  UINT delay() const noexcept;
  HICON read(int size = 0) const noexcept { return _read(_rc[3], size); }
};
