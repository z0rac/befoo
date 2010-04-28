#ifndef H_ICON /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_ICON

#include "win32.h"

/** icon - animation icons
 */
class icon {
  win32::module _mod;
  PWORD _rc;
  struct anim { WORD id, ticks; };
  const anim* _anim;
  int _size;
  int _step;
  HICON _icon;
  void _release();
  HICON _read(int id);
  void _load(int step = 0);
public:
  icon(LPCSTR id, LPCSTR fn = NULL);
  ~icon() { _release(); }
  bool reload(LPCSTR id, LPCSTR fn = NULL);
  operator HICON() const { return _icon; }
  int size() const { return _rc[1]; }
  icon& resize(int size);
  icon& reset(int type);
  icon& next() { _load((_step + 1) % _rc[2]); return *this; }
  UINT delay() const { return _anim[_step].ticks * 50 / 3; };
};

#endif
