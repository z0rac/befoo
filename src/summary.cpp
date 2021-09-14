/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "mailbox.h"
#include "setting.h"
#include "win32.h"
#include "window.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#ifndef HDF_SORTDOWN
#define HDF_SORTDOWN 0x0200
#define HDF_SORTUP   0x0400
#endif

/** summary - summary view window
 */
namespace {
  class summary : public window {
    class item : LVITEMA {
      HWND _h;
    public:
      item(window const& w);
      item& operator()(LPCSTR s);
      item& operator()(LPCWSTR s);
      item& operator()(std::string const& s) { return operator()(s.c_str()); }
      item& operator()(std::wstring const& s) { return operator()(s.c_str()); }
    };
    class compare;
    enum column { SUBJECT, SENDER, DATE, MAILBOX, LAST };
    std::vector<time_t> _dates;
    std::vector<std::pair<size_t, std::string>> _mboxes;
    int _column = 3;
    int _order = 1;
    std::string _tmp;
  protected:
    void release() override;
    LRESULT notify(WPARAM w, LPARAM l) override;
    void _initialize();
    void _sort(int column, int order);
    void _open();
  public:
    summary(window const& parent);
    int initialize(mailbox const* mboxes);
    void raised(bool topmost);
  };
}

summary::summary(window const& parent)
  : window(WC_LISTVIEW, parent)
{
  static commctrl listview { ICC_LISTVIEW_CLASSES };
  style(LVS_REPORT | LVS_SINGLESEL);
  (void)ListView_SetExtendedListViewStyle
    (hwnd(), LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
  setting::preferences("summary")["sort"](_column)(_order);
  show();
}

void
summary::release()
{
  try {
    auto prefs = setting::preferences("summary");
    auto tuple = setting::tuple(ListView_GetColumnWidth(hwnd(), 0));
    for (int i = 1; i < LAST; ++i) tuple(ListView_GetColumnWidth(hwnd(), i));
    if (tuple.row().compare(prefs["columns"])) prefs("columns", tuple);
    tuple = setting::tuple(_column)(_order);
    if (tuple.row().compare(prefs["sort"])) prefs("sort", tuple);
  } catch (...) {}
}

LRESULT
summary::notify(WPARAM w, LPARAM l)
{
  switch (LPNMHDR(l)->code) {
  case LVN_GETDISPINFOA:
    switch (LPNMLVDISPINFOA(l)->item.iSubItem) {
    case DATE:
      if (auto t = _dates[LPNMLVDISPINFOA(l)->item.lParam]; t == time_t(-1)) _tmp = "";
      else _tmp = win32::date(t, DATE_LONGDATE) + " " + win32::time(t, TIME_NOSECONDS);
      LPNMLVDISPINFOA(l)->item.pszText = LPSTR(_tmp.c_str());
      break;
    case MAILBOX:
      for (auto const& mbox : _mboxes) {
	if (mbox.first <= size_t(LPNMLVDISPINFOA(l)->item.lParam)) continue;
	LPNMLVDISPINFOA(l)->item.pszText = LPSTR(mbox.second.c_str());
	break;
      }
      break;
    }
    return 0;
  case LVN_COLUMNCLICK:
    _sort(LPNMLISTVIEW(l)->iSubItem,
	  LPNMLISTVIEW(l)->iSubItem == _column ? -_order : 1);
    return 0;
  case NM_DBLCLK:
    _open();
    return 0;
  }
  return window::notify(w, l);
}

void
summary::_initialize()
{
  auto column = win32::exe.texts(ID_TEXT_SUMMARY_COLUMN);
  int const n = static_cast<int>(column.size());
  auto width = setting::preferences("summary")["columns"].split<int>();
  auto wp = width.begin(), we = width.end();
  LVCOLUMN col { LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM };
  int cx = 0, ex = extent().x;
  for (auto const& name : column) {
    col.cx = wp != we ? *wp++ : col.iSubItem ? (ex - cx) / (n - col.iSubItem) : ex / 2;
    col.pszText = LPSTR(name.c_str());
    (void)ListView_InsertColumn(hwnd(), col.iSubItem, &col);
    cx += col.cx;
    ++col.iSubItem;
  }
}

class summary::compare {
  summary& subject;
  int size[2];
  std::unique_ptr<WCHAR[]> text[2];
public:
  compare(summary& subject, int n = 128)
    : subject(subject), size { n, n }, text {
	std::unique_ptr<WCHAR[]>(new WCHAR[n]),
	std::unique_ptr<WCHAR[]>(new WCHAR[n])
      } {}
  int operator()(LPARAM s1, LPARAM s2) {
    LPARAM si[] { s1, s2 };
    LVITEMW lv { LVIF_TEXT };
    lv.iSubItem = subject._column;
    for (int i = 0; i < 2; ++i) {
      for (lv.cchTextMax = size[i];; size[i] = lv.cchTextMax) {
	lv.pszText = text[i].get();
	auto n = SendMessage(subject.hwnd(), LVM_GETITEMTEXTW, si[i], LPARAM(&lv));
	if (n < lv.cchTextMax - 1) break;
	text[i] = std::unique_ptr<WCHAR[]>(new WCHAR[lv.cchTextMax <<= 1]);
      }
    }
    return lstrcmpiW(text[0].get(), text[1].get()) * subject._order;
  }
};

void
summary::_sort(int column, int order)
{
  auto hdr = ListView_GetHeader(hwnd());
  HDITEM hdi { HDI_FORMAT };
  if (column != _column) {
    (void)Header_GetItem(hdr, _column, &hdi);
    hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
    (void)Header_SetItem(hdr, _column, &hdi);
  }
  (void)Header_GetItem(hdr, column, &hdi);
  hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
  hdi.fmt |= order > 0 ? HDF_SORTUP : HDF_SORTDOWN;
  (void)Header_SetItem(hdr, column, &hdi);
  _column = column, _order = order;
  constexpr int (CALLBACK* cmp[])(LPARAM, LPARAM, LPARAM) = {
    [](auto s1, auto s2, auto arg) {
      auto& s = *reinterpret_cast<summary*>(arg);
      auto v1 = size_t(s._dates[s1]), v2 = size_t(s._dates[s2]);
      return v1 < v2 ? -s._order : int(v1 != v2) * s._order;
    },
    [](auto s1, auto s2, auto arg) {
      auto& s = *reinterpret_cast<summary*>(arg);
      size_t v1 = 0, v2 = 0;
      while (s._mboxes[v1].first <= size_t(s1)) ++v1;
      while (s._mboxes[v2].first <= size_t(s2)) ++v2;
      return v1 < v2 ? -s._order : int(v1 != v2) * s._order;
    },
  };
  if (column == DATE || column == MAILBOX) {
    (void)ListView_SortItems(hwnd(), cmp[column - DATE], LPARAM(this));
  } else {
    compare w { *this };
    (void)ListView_SortItemsEx(hwnd(), [](auto s1, auto s2, auto arg) {
      return (*reinterpret_cast<compare*>(arg))(s1, s2);
    }, LPARAM(&w));
  }
}

void
summary::_open()
{
  int item = ListView_GetNextItem(hwnd(), -1, LVNI_SELECTED);
  if (item < 0) return;
  LVITEM lv { LVIF_PARAM, item };
  if (!ListView_GetItem(hwnd(), &lv)) return;
  for (auto const& mbox : _mboxes) {
    if (mbox.first <= size_t(lv.lParam)) continue;
    std::string mua;
    setting::mailbox(mbox.second)["mua"].sep(0)(mua);
    if (!mua.empty() && win32::shell(mua)) close(true);
    break;
  }
}

int
summary::initialize(mailbox const* mboxes)
{
  int h = extent().y;
  _initialize();
  for (auto mb = mboxes; mb; mb = mb->next()) {
    auto lock = mb->lock();
    LOG("Summary[" << mb->name() << "](" << mb->mails().size() << ")..." << std::endl);
    _mboxes.push_back({ _dates.size() + mb->mails().size(), mb->name() });
    for (auto const& mail : mb->mails()) {
      _dates.push_back(mail.date());
      item(*this)
	(win32::wstring(mail.subject(), CP_UTF8))
	(win32::wstring(mail.sender(), CP_UTF8))
	(LPSTR_TEXTCALLBACK)
	(LPSTR_TEXTCALLBACK);
    }
  }
  _sort(_column, _order);
  int n = ListView_GetItemCount(hwnd()) - 1;
  return HIWORD(ListView_ApproximateViewRect(hwnd(), -1, -1, n)) - h;
}

summary::item::item(window const& w)
  : LVITEMA({}), _h(w.hwnd())
{
  mask = LVIF_PARAM;
  lParam = iItem = ListView_GetItemCount(_h);
  (void)ListView_InsertItem(_h, this);
  mask = LVIF_TEXT;
}

summary::item&
summary::item::operator()(LPCSTR s)
{
  pszText = LPSTR(s);
  SendMessage(_h, LVM_SETITEMA, 0, LPARAM(this));
  ++iSubItem;
  return *this;
}

summary::item&
summary::item::operator()(LPCWSTR s)
{
  pszText = LPSTR(s);
  SendMessage(_h, LVM_SETITEMW, 0, LPARAM(this));
  ++iSubItem;
  return *this;
}

void
summary::raised(bool topmost)
{
  auto h = ListView_GetToolTips(hwnd());
  if (h && ((GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) != topmost) {
    SetWindowPos(h, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
		 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  }
}

/** summarywindow - summary popup window
 */
namespace {
  class summarywindow : public appwindow {
    summary _summary;
    bool _changed;
    struct _autoclose : public window::timer {
      int sec;
      void reset(window& source)
      { if (sec > 0) source.settimer(*this, UINT(sec * 1000)); }
      void wakeup(window& source) override
      { if (source.hascursor()) reset(source); else source.close(); }
    } _autoclose;
    int _alpha;
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l) override;
    void release() override;
    void limit(LPMINMAXINFO info) override;
    void resize(int w, int h) override;
    void raised(bool topmost) override { _summary.raised(topmost); }
  public:
    summarywindow(mailbox const* mboxes);
    ~summarywindow() { if (hwnd()) release(); }
  };
}

summarywindow::summarywindow(mailbox const* mboxes)
  : _summary(self())
{
  DWORD const style(WS_OVERLAPPED | WS_CAPTION |
		    WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN);
  DWORD const exstyle(WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE);
  setting prefs = setting::preferences();
  int transparency;
  prefs["summary"](_autoclose.sec = 3)()(transparency = 0);
  _autoclose.reset(*this);
  _alpha = 255 - 255 * transparency / 100;
  if (_alpha < 255) {
    summarywindow::style(style, exstyle | WS_EX_LAYERED);
    transparent(_alpha);
  } else {
    summarywindow::style(style, exstyle);
  }
  SetWindowText(hwnd(), win32::exe.text(ID_TEXT_SUMMARY_TITLE).c_str());
  RECT r;
  GetWindowRect(GetDesktopWindow(), &r);
  LONG top = r.top, bottom = r.bottom;
  InflateRect(&r, (r.left - r.right) / 4, (r.top - r.bottom) / 4);
  setting::preferences("summary")
    ["window"](r.left)(r.top)(r.right)(r.bottom);
  move(adjust(r));
  int h = _summary.initialize(mboxes);
  if (h > 0) {
    r.bottom += h;
    if (r.bottom > bottom) {
      r.top -= r.bottom - bottom, r.bottom = bottom;
      if (r.top < top) r.top = top;
    }
    move(r);
  }
  topmost(true);
  show(true, GetActiveWindow() == GetForegroundWindow());
  _changed = false;
}

LRESULT
summarywindow::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_MOVE:
    _changed = true;
    break;
  case WM_ENDSESSION:
    if (w) release();
    break;
  case WM_CONTEXTMENU:
    return 0;
  case WM_NCACTIVATE:
    if (_alpha < 255) transparent((!LOWORD(w) - 1) | _alpha);
    break;
  case WM_NCMOUSEMOVE:
    _autoclose.reset(*this);
    break;
  }
  return appwindow::dispatch(m, w, l);
}

void
summarywindow::release()
{
  if (_changed) {
    try {
      auto r = bounds();
      setting::preferences("summary")
	("window", setting::tuple(r.left)(r.top)(r.right)(r.bottom));
    } catch (...) {}
  }
}

void
summarywindow::limit(LPMINMAXINFO info)
{
  info->ptMinTrackSize.x = 200;
  info->ptMinTrackSize.y = 80;
}

void
summarywindow::resize(int w, int h)
{
  auto r = _summary.bounds();
  _changed = r.right - r.left != w || r.bottom - r.top != h;
  _summary.move(0, 0, w, h);
}

window*
summary(mailbox const* mboxes)
{
  return new summarywindow(mboxes);
}
