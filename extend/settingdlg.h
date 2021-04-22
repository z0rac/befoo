/* -*- mode: c++ -*-
 * Copyright (C) 2010-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include "define.h"
#include "win32.h"
#include "setting.h"
#include <windowsx.h>
#include <commctrl.h>

namespace extend { extern win32::module dll; }

/** dialog - base dialog class
 */
class dialog {
  HWND _hwnd = {};
  mutable HWND _tips = {};
  mutable bool _balloon = false;
protected:
  virtual void initialize() {}
  virtual void done(bool ok);
  virtual bool action(int id, int cmd);
  virtual bool drawitem(int id, LPDRAWITEMSTRUCT ctx);
public:
  virtual ~dialog() {}
  auto hwnd() const noexcept { return _hwnd; }
  HWND item(int id) const noexcept { return GetDlgItem(_hwnd, id); }
  void enable(int id, bool en) const noexcept { EnableWindow(item(id), en); }
  void settext(int id, std::string const& text) const noexcept
  { SetDlgItemText(_hwnd, id, text.c_str()); }
  void setspin(int id, int value, int minv = 0, int maxv = UD_MAXVAL) noexcept;
  void seticon(int id, HICON icon) noexcept;
  int getint(int id) const noexcept { return GetDlgItemInt(_hwnd, id, {}, FALSE); }
  std::string gettext(int id) const;
  std::string getfile(int filter, bool quote = false,
		      std::string const& dir = {}) const;
  std::string listitem(int id) const;
  void editselect(int id, int start = 0, int end = -1) const noexcept;
  int msgbox(std::string const& msg, UINT flags = 0) const;
  void balloon(int id, std::string const& msg) const noexcept;
  void clearballoon() const noexcept;
  void error(int id, std::string const& msg, int start = 0, int end = -1) const;
  int modal(int id, HWND parent) noexcept;
};

/** iconspec - icon spec
 */
struct iconspec {
  std::string setting;
  int size = 64;
  HICON symbol = {};
public:
  iconspec() {}
  iconspec(std::string const& setting, int size, HICON symbol)
    : setting(setting), size(size), symbol(symbol) {}
  iconspec(std::string const& setting, int width);
};

/** maindlg - main dialog
 */
class maindlg : public dialog {
  std::string _icon;
  void _delete();
  void _enablebuttons() noexcept;
  int _iconwidth() const noexcept;
  void _enableicon(bool en) noexcept;
protected:
  void initialize() override;
  void done(bool ok) override;
  bool action(int id, int cmd) override;
  void mailbox(bool edit = false);
  void icon();
};
