#ifndef H_SETTINGDLG /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2010-2013 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_SETTINGDLG

#include "define.h"
#include "win32.h"
#include "setting.h"
#include <windowsx.h>
#include <commctrl.h>

namespace extend { extern win32::module dll; }

/** dialog - base dialog class
 */
class dialog {
  HWND _hwnd;
  mutable HWND _tips;
  mutable bool _balloon;
  static INT_PTR CALLBACK _dlgproc(HWND h, UINT m, WPARAM w, LPARAM l);
protected:
  virtual void initialize() {}
  virtual void done(bool ok);
  virtual bool action(int id, int cmd);
  virtual bool drawitem(int id, LPDRAWITEMSTRUCT ctx);
public:
  dialog() : _hwnd(NULL), _tips(NULL), _balloon(false) {}
  virtual ~dialog() {}
  HWND hwnd() const { return _hwnd; }
  HWND item(int id) const { return GetDlgItem(_hwnd, id); }
  void enable(int id, bool en) const { EnableWindow(item(id), en); }
  void settext(int id, const string& text) const
  { SetDlgItemText(_hwnd, id, text.c_str()); }
  void setspin(int id, int value, int minv = 0, int maxv = UD_MAXVAL);
  void seticon(int id, HICON icon);
  int getint(int id) const { return GetDlgItemInt(_hwnd, id, NULL, FALSE); }
  string gettext(int id) const;
  string getfile(int filter, bool quote = false,
		 const string& dir = string()) const;
  string listitem(int id) const;
  void editselect(int id, int start = 0, int end = -1) const;
  int msgbox(const string& msg, UINT flags = 0) const;
  void balloon(int id, const string& msg) const;
  void clearballoon() const;
  void error(int id, const string& msg, int start = 0, int end = -1) const;
  int modal(int id, HWND parent);
};

/** iconspec - icon spec
 */
struct iconspec {
  string setting;
  int size;
  HICON symbol;
public:
  iconspec() : symbol(NULL) {}
  iconspec(const string& setting, int size, HICON symbol)
    : setting(setting), size(size), symbol(symbol) {}
  iconspec(const string& setting, int width);
};

/** maindlg - main dialog
 */
class maindlg : public dialog {
  string _icon;
  void _delete();
  void _enablebuttons();
  int _iconwidth() const;
  void _enableicon(bool en);
protected:
  void initialize();
  void done(bool ok);
  bool action(int id, int cmd);
  void mailbox(bool edit = false);
  void icon();
};

#endif
