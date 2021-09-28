/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"
#include "settingdlg.h"
#include "icon.h"

/** iconlist - list of icons
 */
namespace {
  class iconlist : std::vector<iconspec>, iconmodule::accept {
    void operator()(int id, icon const& icon) override;
  public:
    ~iconlist() { clear(); }
    using std::vector<iconspec>::size_type;
    using std::vector<iconspec>::size;
    using std::vector<iconspec>::operator[];
  public:
    void load();
    void clear();
  };
}

void
iconlist::operator()(int id, icon const& icon)
{
  int const size = min(icon.size(), 64);
  struct spec : public iconspec {
    spec(iconspec const& spec) : iconspec(spec) {}
    ~spec() { symbol && DestroyIcon(symbol); }
  } spec(iconspec(setting::tuple(id), size, icon.read(size)));
  push_back(spec);
  spec.symbol = {};
}

void
iconlist::load()
{
  iconmodule().collect(*this);
  for (auto& s : *this) {
    if (s.setting == "1") s.setting.clear();
  }
  for (auto p = "*.dll\0*.ico\0"; *p; p += strlen(p) + 1) {
    for (win32::find f(iconmodule::path(p)); f; f.next()) {
      auto i = size();
      iconmodule(f.cFileName).collect(*this);
      if (i == size()) continue;
      auto suffix = std::string(",") + PathFindFileName(f.cFileName);
      for (auto& icon : *this) icon.setting += suffix;
    }
  }
}

void
iconlist::clear()
{
  for (auto& icon : *this) DestroyIcon(icon.symbol);
  std::vector<iconspec>::clear();
}

/** icondlg - icon dialog
 */
namespace {
  class icondlg : public dialog {
    std::string _setting;
    iconlist _list;
    void initialize() override;
    void done(bool ok) override;
    bool action(int id, int cmd) override;
    bool drawitem(int id, LPDRAWITEMSTRUCT ctx) override;
  public:
    icondlg(std::string const& setting) : _setting(setting) { _list.load(); }
    auto& setting() const noexcept { return _setting; }
  };
}

void
icondlg::initialize()
{
  auto h = item(IDC_LIST_ICON);
  RECT rc;
  GetClientRect(h, &rc);
  ListBox_SetItemHeight(h, 0, rc.bottom / max(int(rc.bottom / 66), 1));
  ListBox_SetColumnWidth(h, 66);
  for (size_t i = 0; i < _list.size(); ++i) {
    ListBox_AddItemData(h, nullptr);
    if (_list[i].setting == _setting) ListBox_SetCurSel(h, i);
  }
}

void
icondlg::done(bool ok)
{
  if (ok) {
    unsigned i = ListBox_GetCurSel(item(IDC_LIST_ICON));
    if (i >= _list.size()) error(IDC_LIST_ICON, win32::exe.text(IDS_MSG_ITEM_REQUIRED));
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
  auto const& spec = _list[ctx->itemID];
  int x = (ctx->rcItem.left + ctx->rcItem.right - spec.size) >> 1;
  int y = (ctx->rcItem.top + ctx->rcItem.bottom - spec.size) >> 1;
  auto br = GetSysColorBrush(ctx->itemState & ODS_SELECTED ?
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
