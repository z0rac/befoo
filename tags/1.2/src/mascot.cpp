/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "mailbox.h"
#include "setting.h"
#include "icon.h"
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

#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER+0)
#define NIN_KEYSELECT (WM_USER+1)
#endif

#define ICON_BKGC RGB(255, 0, 255)

/** tooltips - tooltips controller
 */
namespace {
  class tooltips : public window, window::timer {
    static commctrl use;
    struct info : public TOOLINFO {
      info(const window& tips);
    };
    class status : public window {
      LRESULT notify(WPARAM w, LPARAM l);
      void tracking(bool tracking);
    public:
      status(const window& owner);
      void operator()(const string& text);
      void reset(bool await = true);
      void disable();
    };
    status _status;
    void wakeup(window&) { clearballoon(); }
    LRESULT notify(WPARAM w, LPARAM l);
  public:
    tooltips(const window& owner);
    void tip(const string& text) { _status(text); }
    void reset(bool await = true) { _status.reset(await); }
    void balloon(const string& text, unsigned sec = 0,
		 const string& title = string(), int icon = 0);
    void clearballoon();
    void topmost(bool owner);
  };
  window::commctrl tooltips::use(ICC_BAR_CLASSES);
}

tooltips::info::info(const window& tips)
{
  ZeroMemory(this, sizeof(TOOLINFO));
  cbSize = sizeof(TOOLINFO);
  hwnd = GetParent(tips.hwnd());
}

tooltips::status::status(const window& owner)
  : window(TOOLTIPS_CLASS, NULL, owner.hwnd())
{
  style(WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX);
  info ti(*this);
  ti.uFlags = TTF_SUBCLASS;
  GetClientRect(ti.hwnd, &ti.rect);
  SendMessage(hwnd(), TTM_ADDTOOL, 0, LPARAM(&ti));
}

void
tooltips::status::operator()(const string& text)
{
  info ti(*this);
  ti.lpszText = LPSTR(text.c_str());
  SendMessage(hwnd(), TTM_UPDATETIPTEXT, 0, LPARAM(&ti));
}

void
tooltips::status::reset(bool await)
{
  if (await) {
    tracking(true);
  } else if (!visible()) {
    for (int i = 0; i < 2; ++i) SendMessage(hwnd(), TTM_ACTIVATE, i, 0);
  }
}

void
tooltips::status::disable()
{
  SendMessage(hwnd(), TTM_ACTIVATE, FALSE, 0);
  tracking(false);
}

void
tooltips::status::tracking(bool tracking)
{
  TRACKMOUSEEVENT tme = {
    sizeof(TRACKMOUSEEVENT),
    tracking ? TME_LEAVE : TME_LEAVE | TME_CANCEL,
    GetParent(hwnd())
  };
  TrackMouseEvent(&tme);
}

LRESULT
tooltips::status::notify(WPARAM w, LPARAM l)
{
  if (LPNMHDR(l)->code == TTN_POP) reset();
  return window::notify(w, l);
}

tooltips::tooltips(const window& owner)
  : window(TOOLTIPS_CLASS, NULL, owner.hwnd()), _status(owner)
{
  style(WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON | TTS_CLOSE);
  info ti(*this);
  ti.uFlags = TTF_TRACK;
  SendMessage(hwnd(), TTM_ADDTOOL, 0, LPARAM(&ti));
  SendMessage(hwnd(), TTM_SETMAXTIPWIDTH, 0, 300);
}

void
tooltips::balloon(const string& text, unsigned sec, const string& title, int icon)
{
  info ti(*this);
  SendMessage(hwnd(), TTM_TRACKACTIVATE, FALSE, LPARAM(&ti));
  _status.disable();
  RECT r;
  GetClientRect(ti.hwnd, &r);
  r.left = r.right / 2, r.top = r.bottom * 9 / 16;
  ClientToScreen(ti.hwnd, LPPOINT(&r));
  SendMessage(hwnd(), TTM_TRACKPOSITION, 0, MAKELPARAM(r.left, r.top));
  ti.lpszText = LPSTR(text.c_str());
  SendMessage(hwnd(), TTM_UPDATETIPTEXT, 0, LPARAM(&ti));
  SendMessage(hwnd(), TTM_SETTITLEA, WPARAM(icon), LPARAM(title.c_str()));
  SendMessage(hwnd(), TTM_TRACKACTIVATE, TRUE, LPARAM(&ti));
  settimer(*this, sec * 1000);
}

void
tooltips::clearballoon()
{
  info ti(*this);
  SendMessage(hwnd(), TTM_TRACKACTIVATE, FALSE, LPARAM(&ti));
}

void
tooltips::topmost(bool owner)
{
  if (window::topmost() != owner) window::topmost(owner);
  if (_status.topmost() != owner) _status.topmost(owner);
}

LRESULT
tooltips::notify(WPARAM w, LPARAM l)
{
  if (LPNMHDR(l)->code == TTN_POP) _status.reset();
  return window::notify(w, l);
}

/** iconwindow - icon window with system tray
 */
namespace {
  class iconwindow : public appwindow, window::timer {
    icon _icon;
    tooltips _tips;
    string _status;
    UINT _tbcmsg;
    bool _trayicon(bool tray);
    void _update();
    void _updatetips();
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
    void release() { _trayicon(false); }
    void draw(HDC hDC);
    void raised(bool topmost) { _tips.topmost(topmost); }
    bool popup(const menu& menu, DWORD pt);
    void wakeup(window& source);
    void reset(int type);
    void status(const string& text);
    void balloon(const string& text, unsigned sec,
		 const string& title = string(), int icon = 0);
    int size() const { return _icon.size(); }
  public:
    iconwindow(const icon& icon);
    ~iconwindow() { if (hwnd()) _trayicon(false); }
    void trayicon(bool tray);
    bool intray() const { return !visible(); }
    void transparent(int alpha) { appwindow::transparent(alpha, ICON_BKGC); }
  };
}

bool
iconwindow::_trayicon(bool tray)
{
  NOTIFYICONDATA ni = { sizeof(NOTIFYICONDATA), hwnd() };
  if (tray) {
    ni.uFlags = NIF_MESSAGE | NIF_ICON;
    ni.uCallbackMessage = WM_USER;
    ni.hIcon = _icon;
    while (!Shell_NotifyIcon(NIM_ADD, &ni)) {
      if (GetLastError() != ERROR_TIMEOUT) return false;
      if (Shell_NotifyIcon(NIM_MODIFY, &ni)) break;
      Sleep(1000);
    }
    ni.uVersion = NOTIFYICON_VERSION;
    Shell_NotifyIcon(NIM_SETVERSION, &ni);
  } else {
    Shell_NotifyIcon(NIM_DELETE, &ni);
  }
  return true;
}

void
iconwindow::_update()
{
  if (intray()) {
    NOTIFYICONDATA ni = { sizeof(NOTIFYICONDATA), hwnd() };
    ni.uFlags = NIF_ICON;
    ni.hIcon = _icon;
    Shell_NotifyIcon(NIM_MODIFY, &ni);
  } else {
    invalidate();
  }
}

void
iconwindow::_updatetips()
{
  if (intray()) {
    NOTIFYICONDATA ni = { sizeof(NOTIFYICONDATA), hwnd() };
    ni.uFlags = NIF_TIP;
    lstrcpyn(ni.szTip, _status.c_str(), sizeof(ni.szTip));
    Shell_NotifyIcon(NIM_MODIFY, &ni);
  } else {
    _tips.tip(_status);
  }
}

LRESULT
iconwindow::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN:
  case WM_SHOWWINDOW:
    _tips.clearballoon();
    // fall down
  case WM_MOUSELEAVE:
    _tips.reset(hascursor());
    break;
  case WM_USER: // from tray icon
    switch (l) {
    case WM_CONTEXTMENU: break;
    case NIN_SELECT: l = WM_LBUTTONDOWN; break;
    case NIN_KEYSELECT: l = WM_LBUTTONDBLCLK; break;
    default: return 0;
    }
    POINT pt;
    GetCursorPos(&pt);
    foreground();
    PostMessage(hwnd(), l, 0, MAKELPARAM(pt.x, pt.y));
    return 0;
  default:
    if (m == _tbcmsg && intray()) _trayicon(true);
    break;
  }
  return appwindow::dispatch(m, w, l);
}

void
iconwindow::draw(HDC hDC)
{
  RECT r;
  GetClientRect(hwnd(), &r);
  HBRUSH br = CreateSolidBrush(ICON_BKGC);
  DrawIconEx(hDC, r.left, r.top, _icon, r.right, r.bottom, 0, br, DI_NORMAL);
  DeleteObject(HGDIOBJ(br));
}

bool
iconwindow::popup(const menu& menu, DWORD pt)
{
  bool t = appwindow::popup(menu, pt);
  if (!t && intray()) {
    NOTIFYICONDATA ni = { sizeof(NOTIFYICONDATA), hwnd() };
    Shell_NotifyIcon(NIM_SETFOCUS, &ni);
  }
  return t;
}

void
iconwindow::wakeup(window& source)
{
  source.settimer(*this, _icon.next().delay());
  _update();
}

void
iconwindow::reset(int type)
{
  settimer(*this, _icon.reset(type).delay());
  _update();
}

void
iconwindow::status(const string& text)
{
  _status = text;
  _tips.clearballoon();
  _updatetips();
}

void
iconwindow::balloon(const string& text, unsigned sec,
		    const string& title, int icon)
{
  if (intray()) {
    NOTIFYICONDATA ni = { sizeof(NOTIFYICONDATA), hwnd() };
    ni.uFlags = NIF_INFO;
    lstrcpyn(ni.szInfo, text.c_str(), sizeof(ni.szInfo));
    ni.uTimeout = sec * 1000;
    lstrcpyn(ni.szInfoTitle, title.c_str(), sizeof(ni.szInfoTitle));
    ni.dwInfoFlags = icon & 3;
    Shell_NotifyIcon(NIM_MODIFY, &ni);
  } else {
    _tips.balloon(text, sec, title, icon);
  }
}

iconwindow::iconwindow(const icon& icon)
  : _icon(icon), _tips(self()), _tbcmsg(RegisterWindowMessage("TaskbarCreated"))
{
  style(WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_LAYERED);
}

void
iconwindow::trayicon(bool tray)
{
  _icon.resize(tray ? GetSystemMetrics(SM_CXSMICON) : _icon.size());
  if (_trayicon(tray)) {
    show(!tray);
    _updatetips();
  } else {
    _icon.resize(_icon.size());
  }
}

/** mascotwindow - mascot window
 */
namespace {
  class mascotwindow : public iconwindow {
    menu _menu;
    int _balloon;
    void _release();
    static icon _icon();
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
    void release();
    void update(int recent, int unseen, list<mailbox*>* mboxes);
  public:
    mascotwindow();
    ~mascotwindow() { if (hwnd()) _release(); }
    using iconwindow::balloon;
  };
}

void
mascotwindow::_release()
{
  try {
    RECT dt;
    GetWindowRect(GetDesktopWindow(), &dt);
    dt.right -= dt.left, dt.bottom -= dt.top;
    RECT r = bounds();
    setting::preferences("mascot")
      ("position", setting::tuple
       (r.left - dt.left)(r.top - dt.top)(dt.right)(dt.bottom)(topmost()))
      ("tray", intray());
  } catch (...) {}
}

icon
mascotwindow::_icon()
{
  try {
    int id;
    string fn;
    setting::preferences()["icon"]()()(id = 1).sep(0)(fn);
    return icon(id, fn);
  } catch (...) {
    return icon(1);
  }
}

LRESULT
mascotwindow::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_ENDSESSION:
    if (w) {
      _release();
      execute(ID_EVENT_LOGOFF);
    }
    break;
  case WM_LBUTTONDOWN:
    if (!intray()) {
      ReleaseCapture();
      PostMessage(hwnd(), WM_SYSCOMMAND, SC_MOVE | HTCAPTION, l);
      break;
    }
    // fall down
  case WM_LBUTTONDBLCLK:
    execute(_menu);
    return 0;
  case WM_CONTEXTMENU:
    if (l == ~LPARAM(0) && !intray()) {
      POINT pt = extent();
      pt.x >>= 1, pt.y >>= 1;
      ClientToScreen(hwnd(), &pt);
      l = MAKELPARAM(pt.x, pt.y);
    }
    popup(_menu, l);
    return 0;
  case WM_APP: // broadcast
    update(LOWORD(w), HIWORD(w), reinterpret_cast<list<mailbox*>*>(l));
    return 0;
  }
  return iconwindow::dispatch(m, w, l);
}

void
mascotwindow::release()
{
  iconwindow::release();
  _release();
  PostQuitMessage(0);
}

void
mascotwindow::update(int recent, int unseen, list<mailbox*>* mboxes)
{
  if (mboxes) {
    LOG("Update: " << recent << ", " << unseen << endl);
    string info;
    bool newer = false;
    list<mailbox*>::const_iterator p = mboxes->begin();
    for (; p != mboxes->end(); ++p) {
      int n = (*p)->recent();
      if (n && (_balloon || n < 0)) {
	info += win32::exe.textf(n < 0 ? ID_TEXT_FETCH_ERROR :
				 ID_TEXT_FETCHED_MAIL,
				 n, (*p)->mails().size());
	info += " @ " + (*p)->name() + "\n";
      }
      newer = newer || n > 0;
    }
    ReplyMessage(0);
    status(win32::exe.textf(ID_TEXT_FETCHED_MAIL, recent, unseen));
    if (!info.empty()) {
      balloon(info.erase(info.size() - 1), _balloon ? _balloon : 10,
	      win32::exe.text(newer ? ID_TEXT_BALLOON_TITLE :
			      ID_TEXT_BALLOON_ERROR),
	      newer ? NIIF_INFO : NIIF_ERROR);
    }
    reset((unseen == 0) + 1);
  } else {
    status(win32::exe.text(ID_TEXT_FETCHING));
    reset(0);
  }
}

mascotwindow::mascotwindow()
  : iconwindow(_icon()), _menu(MAKEINTRESOURCE(1))
{
  setting prefs = setting::preferences();
  int icon, transparency;
  prefs["icon"](icon = size())(transparency = 0);
  if (!icon) icon = GetSystemMetrics(SM_CXICON);
  prefs["balloon"](_balloon = 10);

  prefs = setting::preferences("mascot");
  RECT dt;
  GetWindowRect(GetDesktopWindow(), &dt);
  dt.right -= dt.left, dt.bottom -= dt.top;
  RECT r = { dt.right - icon, dt.top, dt.right, dt.bottom };
  int raise, tray;
  prefs["position"](r.left)(r.top)(r.right)(r.bottom)(raise = 0);
  prefs["tray"](tray = 0);
  r.left = dt.left + MulDiv(r.left, dt.right, r.right);
  r.top = dt.top + MulDiv(r.top, dt.bottom, r.bottom);
  r.right = r.left + icon;
  r.bottom = r.top + icon;
  move(adjust(r, icon / 4));
  transparent(255 - 255 * transparency / 100);
  topmost(raise != 0);
  trayicon(tray != 0);
}

namespace cmd {
  class trayicon : public window::command {
    void execute(window& source)
    { ((mascotwindow&)source).trayicon(!((mascotwindow&)source).intray()); }
    UINT state(window& source)
    { return ((mascotwindow&)source).intray() ? MFS_CHECKED : 0; }
  };

  class alwaysontop : public window::command {
    void execute(window& source) { source.topmost(!source.topmost()); }
    UINT state(window& source)
    {
      return ((mascotwindow&)source).intray() ?
	MFS_DISABLED : source.topmost() ? MFS_CHECKED : 0;
    }
  };

  class about : public window::command {
    void execute(window& source)
    {
      ((mascotwindow&)source).balloon(win32::exe.text(ID_TEXT_ABOUT), 10,
				      win32::exe.text(ID_TEXT_VERSION));
    }
  };
}

window*
mascot()
{
  auto_ptr<mascotwindow> w(new mascotwindow);
  w->addcmd(ID_MENU_TRAYICON, new cmd::trayicon);
  w->addcmd(ID_MENU_ALWAYSONTOP, new cmd::alwaysontop);
  w->addcmd(ID_MENU_ABOUT, new cmd::about);
  if (string(setting::preferences("mascot")["tray"]).empty()) {
    w->execute(ID_MENU_ABOUT);
  }
  return w.release();
}
