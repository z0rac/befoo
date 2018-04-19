#ifndef H_ICON /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2010-2011 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_ICON

#include "win32.h"

/** iconmodule - module includes animation icons
 */
class icon;
class iconmodule {
  struct rep {
    HMODULE module;
    size_t count;
    rep(HMODULE module) : module(module), count(1) {}
    ~rep() { module && module != win32::exe && FreeLibrary(module); }
  };
  rep* _rep;
  void _release();
public:
  iconmodule(const string& fn = string());
  iconmodule(const iconmodule& module);
  ~iconmodule() { _release(); }
  iconmodule& operator=(const iconmodule& module);
  operator HMODULE() const { return _rep->module; }
  static string path(LPCSTR fn = NULL);
public:
  struct accept {
    virtual void operator()(int id, const icon& icon) = 0;
  };
  void collect(accept& accept) const;
};

/** icon - animation icons
 */
class icon {
  iconmodule _mod;
  PWORD _rc;
  struct anim { WORD id, ticks; };
  const anim* _anim;
  int _size;
  int _step;
  HICON _icon;
  HICON _read(int id, int size = 0) const;
  icon& _load(int step = 0);
public:
  icon(int id, const iconmodule& mod = iconmodule());
  icon(const icon& copy) : _icon(NULL) { *this = copy; }
  ~icon();
  icon& operator=(const icon& copy);
  operator HICON() const { return _icon; }
  int size() const { return _rc[1]; }
  icon& resize(int size);
  icon& reset() { return _load(_step); }
  icon& reset(int type);
  icon& next();
  UINT delay() const;
  HICON read(int size = 0) const { return _read(_rc[3], size); }
};

#endif
