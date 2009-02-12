/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
#define LOG(s) (cout << s)
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
      item(const window& w);
      item& operator()(LPCSTR s);
      item& operator()(LPCWSTR s);
      item& operator()(const string& s) { return operator()(s.c_str()); }
    };
    enum column { SUBJECT, SENDER, DATE, MAILBOX, LAST };
    time_t* _dates;
    pair<size_t, string>* _mboxes;
    int _column;
    int _order;
    string _tmp;
    int _compare(LPARAM s1, LPARAM s2) const;
    static int CALLBACK compare(LPARAM l1, LPARAM l2, LPARAM lsort);
  protected:
    void release();
    LRESULT notify(WPARAM w, LPARAM l);
    void _initialize();
    void _sort(int column, int order);
    void _open();
  public:
    summary(const window& parent);
    ~summary();
    int initialize(const mailbox* mboxes);
  };
}

summary::summary(const window& parent)
  : window(WC_LISTVIEW, parent), _dates(NULL), _mboxes(NULL),
    _column(3), _order(1)
{
  static commctrl listview(ICC_LISTVIEW_CLASSES);
  style(LVS_REPORT | LVS_SINGLESEL, WS_EX_CLIENTEDGE);
  ListView_SetExtendedListViewStyle
    (hwnd(), LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
  setting::preferences("summary")["sort"](_column)(_order);
  show();
}

summary::~summary()
{
  delete [] _dates;
  delete [] _mboxes;
}

void
summary::release()
{
  try {
    setting::tuple columns(ListView_GetColumnWidth(hwnd(), 0));
    for (int i = 1; i < LAST; ++i) {
      columns(ListView_GetColumnWidth(hwnd(), i));
    }
    setting::preferences("summary")
      ("columns", columns)
      ("sort", setting::tuple(_column)(_order));
  } catch (...) {}
}

LRESULT
summary::notify(WPARAM w, LPARAM l)
{
  switch (LPNMHDR(l)->code) {
  case LVN_GETDISPINFOA:
    switch (LPNMLVDISPINFOA(l)->item.iSubItem) {
    case DATE:
      {
	time_t t = _dates[LPNMLVDISPINFOA(l)->item.lParam];
	_tmp = t == time_t(-1) ? "" :
	  win32::date(t, DATE_LONGDATE) + " " + win32::time(t, TIME_NOSECONDS);
	LPNMLVDISPINFOA(l)->item.pszText = LPSTR(_tmp.c_str());
      }
      break;
    case MAILBOX:
      {
	const pair<size_t, string>* p = _mboxes;
	while (size_t(LPNMLVDISPINFOA(l)->item.lParam) >= p->first) ++p;
	LPNMLVDISPINFOA(l)->item.pszText = LPSTR(p->second.c_str());
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
  list<string> column = win32::exe.texts(ID_TEXT_SUMMARY_COLUMN);
  const int n = column.size();
  list<int> width = setting::preferences("summary")["columns"].split<int>();
  list<int>::iterator wp = width.begin();
  LVCOLUMN col = { LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM };
  int cx = 0, ex = extent().x;
  for (list<string>::iterator p = column.begin(); p != column.end(); ++p) {
    col.cx = (wp != width.end() ? *wp++ :
	      col.iSubItem ? (ex - cx) / (n - col.iSubItem) : ex / 2);
    col.pszText = LPSTR(p->c_str());
    ListView_InsertColumn(hwnd(), col.iSubItem, &col);
    cx += col.cx;
    ++col.iSubItem;
  }
}

void
summary::_sort(int column, int order)
{
  HWND hdr = ListView_GetHeader(hwnd());
  HDITEM hdi = { HDI_FORMAT };
  if (column != _column) {
    Header_GetItem(hdr, _column, &hdi);
    hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
    Header_SetItem(hdr, _column, &hdi);
  }
  Header_GetItem(hdr, column, &hdi);
  hdi.fmt &= ~(HDF_SORTDOWN | HDF_SORTUP);
  hdi.fmt |= order > 0 ? HDF_SORTUP : HDF_SORTDOWN;
  Header_SetItem(hdr, column, &hdi);
  _column = column, _order = order;
  SendMessage(hwnd(), column < DATE ? LVM_SORTITEMSEX : LVM_SORTITEMS,
	      WPARAM(this), LPARAM(&compare));
}

inline int
summary::_compare(LPARAM s1, LPARAM s2) const
{
  switch (_column) {
  case DATE:
    {
      size_t v1 = size_t(_dates[s1]);
      size_t v2 = size_t(_dates[s2]);
      return v1 < v2 ? -_order : int(v1 != v2) * _order;
    }
  case MAILBOX:
    {
      size_t v1 = 0, v2 = 0;
      while (_mboxes[v1].first <= size_t(s1)) ++v1;
      while (_mboxes[v2].first <= size_t(s2)) ++v2;
      return v1 < v2 ? -_order : int(v1 != v2) * _order;
    }
  }

  struct textbuf {
    LPWSTR data;
    textbuf() : data(NULL) {}
    ~textbuf() { delete [] data; }
    LPWSTR operator()(size_t n)
    {
      assert(n);
      delete [] data, data = NULL;
      return data = new WCHAR[n];
    }
  } tb[2];
  int si[] = { s1, s2 };
  for (int i = 0; i < 2; ++i) {
    LVITEMW lv = { LVIF_TEXT };
    lv.iSubItem = _column;
    lv.cchTextMax = 256;
    int n;
    do {
      lv.pszText = tb[i](lv.cchTextMax <<= 1);
      n = SendMessage(hwnd(), LVM_GETITEMTEXTW, si[i], LPARAM(&lv));
    } while (n == lv.cchTextMax - 1);
  }
  return _wcsicmp(tb[0].data, tb[1].data) * _order;
}

int CALLBACK
summary::compare(LPARAM l1, LPARAM l2, LPARAM lsort)
{
  return reinterpret_cast<summary*>(lsort)->_compare(l1, l2);
}

void
summary::_open()
{
  int item = ListView_GetNextItem(hwnd(), WPARAM(-1), LVNI_SELECTED);
  if (item >= 0) {
    LVITEM lv = { LVIF_PARAM, item };
    if (ListView_GetItem(hwnd(), &lv)) {
      size_t i = 0;
      while (_mboxes[i].first <= size_t(lv.lParam)) ++i;
      string mua = setting::mailbox(_mboxes[i].second)["mua"];
      if (!mua.empty()) win32::shell(mua);
    }
  }
}

int
summary::initialize(const mailbox* mboxes)
{
  int h = extent().y;
  _initialize();
  size_t mails = 0;
  int mbn = 0;
  for (const mailbox* mb = mboxes; mb; mb = mb->next()) {
    mails += mb->mails().size();
    ++mbn;
  }
  assert(!_dates && !_mboxes);
  _dates = new time_t[mails];
  _mboxes = new pair<size_t, string>[mbn];
  mails = 0, mbn = 0;
  for (const mailbox* mb = mboxes; mb; mb = mb->next()) {
    LOG("Summary[" << mb->name() << "](" << mb->mails().size() << ")..." << endl);
    _mboxes[mbn].first = mails + mb->mails().size();
    _mboxes[mbn++].second = mb->name();
    list<mail>::const_iterator mp = mb->mails().begin();
    for (; mp != mb->mails().end(); ++mp) {
      _dates[mails++] = mp->date();
      item(*this)
	(win32::wstr(mp->subject(), CP_UTF8))
	(win32::wstr(mp->sender(), CP_UTF8))
	(LPSTR_TEXTCALLBACK)
	(LPSTR_TEXTCALLBACK);
    }
  }
  _sort(_column, _order);
  int n = ListView_GetItemCount(hwnd()) - 1;
  return HIWORD(ListView_ApproximateViewRect(hwnd(), -1, -1, n)) - h;
}

summary::item::item(const window& w)
  : _h(w.hwnd())
{
  ZeroMemory(this, sizeof(LVITEMA));
  mask = LVIF_PARAM;
  iItem = lParam = ListView_GetItemCount(_h);
  ListView_InsertItem(_h, this);
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

/** summarywindow - summary popup window
 */
namespace {
  class summarywindow : public appwindow {
    summary _summary;
    int _resized;
    struct _autoclose : public window::timer {
      int sec;
      void reset(window& source)
      { if (sec > 0) source.settimer(*this, UINT(sec * 1000)); }
      void wakeup(window& source)
      { if (source.hascursor()) reset(source); else source.close(); }
    } _autoclose;
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
    void release();
    void limit(LPMINMAXINFO info);
    void resize(int w, int h);
  public:
    summarywindow(const mailbox* mboxes);
    ~summarywindow() { if (hwnd()) release(); }
  };
}

summarywindow::summarywindow(const mailbox* mboxes)
  : _summary(self()), _resized(-1)
{
  style(WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
	WS_THICKFRAME | WS_CLIPCHILDREN,
	WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_WINDOWEDGE);
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
  setting::preferences()["summary"](_autoclose.sec = 3);
  _autoclose.reset(*this);
  show(true, false);
  foreground(true);
  _resized = 0;
}

LRESULT
summarywindow::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_SETCURSOR:
    _autoclose.reset(*this);
    break;
  case WM_CONTEXTMENU:
    return 0;
  }
  return appwindow::dispatch(m, w, l);
}

void
summarywindow::release()
{
  if (_resized > 0) {
    try {
      RECT r = bounds();
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
  if (!_resized) {
    RECT r = _summary.bounds();
    _resized = int(r.right - r.left != w || r.bottom - r.top != h) ;
  }
  _summary.move(0, 0, w, h);
}

window*
summary(const mailbox* mboxes)
{
  return new summarywindow(mboxes);
}
