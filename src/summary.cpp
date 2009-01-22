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

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** summary - summary view window
 */
namespace {
  class summary : public window {
    class item : LVITEMA {
      HWND _h;
    public:
      item(const window& w);
      item& operator()(const string& s);
      item& operator()(const win32::wstr& s);
    };
    void release();
    void _initialize();
  public:
    summary(const window& parent);
    int initialize(const mailbox* mboxes);
  };
}

summary::summary(const window& parent)
  : window(WC_LISTVIEW, parent)
{
  static commctrl listview(ICC_LISTVIEW_CLASSES);
  style(LVS_REPORT | LVS_NOSORTHEADER, WS_EX_CLIENTEDGE);
  ListView_SetExtendedListViewStyle
    (hwnd(), LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_LABELTIP);
  show();
}

void
summary::release()
{
  try {
    setting::tuple tuple(ListView_GetColumnWidth(hwnd(), 0));
    for (int i = 1; i < 4; ++i) {
      tuple(ListView_GetColumnWidth(hwnd(), i));
    }
    setting::preferences("summary")("columns", tuple);
  } catch (...) {}
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

int
summary::initialize(const mailbox* mboxes)
{
  int h = extent().y;
  _initialize();
  for (const mailbox* mb = mboxes; mb; mb = mb->next()) {
    const list<mail>& mails = mb->mails();
    LOG("Summary[" << mb->name() << "](" << mails.size() << ")..." << endl);
    list<mail>::const_iterator mp = mails.begin();
    for (; mp != mails.end(); ++mp) {
      item(*this)
	(win32::wstr(mp->subject(), CP_UTF8))
	(win32::wstr(mp->sender(), CP_UTF8))
	(mp->date() == time_t(-1) ? "" :
	 win32::date(mp->date(), DATE_LONGDATE) + " " +
	 win32::time(mp->date(), TIME_NOSECONDS))
	(mb->name());
    }
  }
  int n = ListView_GetItemCount(hwnd()) - 1;
  return HIWORD(ListView_ApproximateViewRect(hwnd(), -1, -1, n)) - h;
}

summary::item::item(const window& w)
  : _h(w.hwnd())
{
  ZeroMemory(this, sizeof(LVITEMA));
  mask = LVIF_TEXT;
  iItem = ListView_GetItemCount(_h);
}

summary::item&
summary::item::operator()(const string& s)
{
  pszText = LPSTR(s.c_str());
  SendMessage(_h, iSubItem ? LVM_SETITEMA : LVM_INSERTITEMA, 0, LPARAM(this));
  ++iSubItem;
  return *this;
}

summary::item&
summary::item::operator()(const win32::wstr& s)
{
  pszText = LPSTR(LPCWSTR(s));
  SendMessage(_h, iSubItem ? LVM_SETITEMW : LVM_INSERTITEMW, 0, LPARAM(this));
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