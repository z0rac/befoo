/*
 * Copyright (C) 2009-2016 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "settingdlg.h"
#include "icon.h"
#include <cassert>
#include <vector>
#include <shlwapi.h>

/** iconlist - list of icons
 */
namespace {
  class iconlist : vector<iconspec>, iconmodule::accept {
    void operator()(int id, const icon& icon);
  public:
    iconlist() {}
    ~iconlist() { clear(); }
    using vector<iconspec>::size_type;
    using vector<iconspec>::size;
    using vector<iconspec>::operator[];
  public:
    void load();
    void clear();
  };
}

void
iconlist::operator()(int id, const icon& icon)
{
  const int size = min(icon.size(), 64);
  struct spec : public iconspec {
    spec(const iconspec& spec) : iconspec(spec) {}
    ~spec() { symbol && DestroyIcon(symbol); }
  } spec(iconspec(setting::tuple(id), size, icon.read(size)));
  push_back(spec);
  spec.symbol = NULL;
}

void
iconlist::load()
{
  iconmodule().collect(*this);
  for (vector<iconspec>::iterator p = begin(); p != end(); ++p) {
    if (p->setting == "1") p->setting.clear();
  }
  for (const char* p = "*.dll\0*.ico\0"; *p; p += strlen(p) + 1) {
    for (win32::find f(iconmodule::path(p)); f; f.next()) {
      iconlist::size_type i = size();
      iconmodule(f.cFileName).collect(*this);
      if (i == size()) continue;
      string suffix = string(",") + PathFindFileName(f.cFileName);
      vector<iconspec>::iterator it = begin() + i;
      for (; it != end(); ++it) it->setting += suffix;
    }
  }
}

void
iconlist::clear()
{
  vector<iconspec>::iterator p = begin();
  for (; p != end(); ++p) DestroyIcon(p->symbol);
  vector<iconspec>::clear();
}

/** icondlg - icon dialog
 */
namespace {
  class icondlg : public dialog {
    string _setting;
    iconlist _list;
    void initialize();
    void done(bool ok);
    bool action(int id, int cmd);
    bool drawitem(int id, LPDRAWITEMSTRUCT ctx);
  public:
    icondlg(const string& setting) : _setting(setting) { _list.load(); }
    const string& setting() const { return _setting; }
  };
}

void
icondlg::initialize()
{
  HWND h = item(IDC_LIST_ICON);
  RECT rc;
  GetClientRect(h, &rc);
  ListBox_SetItemHeight(h, 0, rc.bottom / max(int(rc.bottom / 66), 1));
  ListBox_SetColumnWidth(h, 66);
  for (iconlist::size_type i = 0; i < _list.size(); ++i) {
    ListBox_AddItemData(h, NULL);
    if (_list[i].setting == _setting) ListBox_SetCurSel(h, i);
  }
}

void
icondlg::done(bool ok)
{
  if (ok) {
    unsigned i = ListBox_GetCurSel(item(IDC_LIST_ICON));
    if (i >= _list.size()) error(IDC_LIST_ICON, extend::dll.text(IDS_MSG_ITEM_REQUIRED));
    _setting = _list[i].setting;
  }
  dialog::done(ok);
}

bool
icondlg::action(int id, int cmd)
{
  if (id == IDC_LIST_ICON && cmd == LBN_DBLCLK) id = IDOK;
  return dialog::action(id, cmd);
}

bool
icondlg::drawitem(int, LPDRAWITEMSTRUCT ctx)
{
  if (ctx->itemID == UINT(-1)) return true;
  const iconspec& spec = _list[ctx->itemID];
  int x = (ctx->rcItem.left + ctx->rcItem.right - spec.size) >> 1;
  int y = (ctx->rcItem.top + ctx->rcItem.bottom - spec.size) >> 1;
  HBRUSH br = GetSysColorBrush(ctx->itemState & ODS_SELECTED ?
			       COLOR_HIGHLIGHT : COLOR_WINDOW);
  FillRect(ctx->hDC, &ctx->rcItem, br);
  DrawIconEx(ctx->hDC, x, y, spec.symbol, 0, 0, 0, br, DI_NORMAL);
  return true;
}

/** Functions of the class main dialog
 */
void
maindlg::icon()
{
  icondlg dlg(_icon);
  if (!dlg.modal(IDD_ICON, hwnd())) return;
  _icon = dlg.setting();
  iconspec icon(_icon, _iconwidth());
  seticon(IDC_BUTTON_ICON, icon.symbol);
  if (!Button_GetCheck(item(IDC_CHECKBOX_ICON))) {
    setspin(IDC_SPIN_ICON, icon.size, 0, 256);
  }
}
