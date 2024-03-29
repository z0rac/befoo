/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"
#include "icon.h"

#define ICON_BKGC RGB(255, 0, 255)

/** tooltips - tooltips controller
 */
namespace {
  class tooltips : public window, window::timer {
    static commctrl use;
    struct info : public TOOLINFO {
      info(window const& tips, UINT flags = 0);
    };
    class status : public window {
      LRESULT notify(WPARAM w, LPARAM l) override;
      void tracking(bool tracking);
    public:
      status(window const& owner);
      void operator()(std::string const& text);
      void reset(bool await = true);
      void disable();
    };
    status _status;
    void wakeup(window&) override { clearballoon(); }
    LRESULT notify(WPARAM w, LPARAM l) override;
  public:
    tooltips(window const& owner);
    void tip(std::string const& text) { _status(text); }
    void reset(bool await = true) { _status.reset(await); }
    void balloon(std::wstring const& text, unsigned sec = 0,
		 std::wstring const& title = {}, int icon = 0);
    void clearballoon();
    void topmost(bool owner);
  public:
    class ellipsis {
      HDC hDC;
      HGDIOBJ hFont;
    public:
      ellipsis();
      ~ellipsis();
      std::wstring operator()(std::wstring_view ws) const;
    };
  };
  window::commctrl tooltips::use(ICC_BAR_CLASSES);
}

tooltips::info::info(window const& tips, UINT flags)
  : TOOLINFO({ sizeof(TOOLINFO), flags, GetParent(tips.hwnd()) }) {}

tooltips::status::status(window const& owner)
  : window(TOOLTIPS_CLASS, {}, owner.hwnd())
{
  style(WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX);
  info ti(*this, TTF_SUBCLASS);
  GetClientRect(ti.hwnd, &ti.rect);
  SendMessage(hwnd(), TTM_ADDTOOL, 0, LPARAM(&ti));
}

void
tooltips::status::operator()(std::string const& text)
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
  TRACKMOUSEEVENT tme
    { sizeof(tme), tracking ? TME_LEAVE : TME_LEAVE | TME_CANCEL, GetParent(hwnd()) };
  TrackMouseEvent(&tme);
}

LRESULT
tooltips::status::notify(WPARAM w, LPARAM l)
{
  if (LPNMHDR(l)->code == TTN_POP) reset();
  return window::notify(w, l);
}

tooltips::tooltips(window const& owner)
  : window(TOOLTIPS_CLASS, {}, owner.hwnd()), _status(owner)
{
  style(WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX | TTS_BALLOON | TTS_CLOSE);
  info ti(*this, TTF_TRACK);
  SendMessage(hwnd(), TTM_ADDTOOL, 0, LPARAM(&ti));
  SendMessage(hwnd(), TTM_SETMAXTIPWIDTH, 0, 300);
}

void
tooltips::balloon(std::wstring const& text, unsigned sec,
		  std::wstring const& title, int icon)
{
  TOOLINFOW ti { sizeof(ti), 0, GetParent(hwnd()) };
  SendMessage(hwnd(), TTM_TRACKACTIVATE, FALSE, LPARAM(&ti));
  _status.disable();
  RECT r;
  GetClientRect(ti.hwnd, &r);
  r.left = r.right / 2, r.top = r.bottom * 9 / 16;
  ClientToScreen(ti.hwnd, LPPOINT(&r));
  SendMessage(hwnd(), TTM_TRACKPOSITION, 0, MAKELPARAM(r.left, r.top));
  ti.lpszText = LPWSTR(text.c_str());
  SendMessage(hwnd(), TTM_UPDATETIPTEXTW, 0, LPARAM(&ti));
  SendMessage(hwnd(), TTM_SETTITLEW, WPARAM(icon), LPARAM(title.c_str()));
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

tooltips::ellipsis::ellipsis()
  : hDC(CreateCompatibleDC({}))
{
  NONCLIENTMETRICS ncm { sizeof(ncm) };
  SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
  hFont = SelectObject(hDC, CreateFontIndirect(&ncm.lfStatusFont));
}

tooltips::ellipsis::~ellipsis()
{
  DeleteObject(SelectObject(hDC, hFont));
  DeleteDC(hDC);
}

std::wstring
tooltips::ellipsis::operator()(std::wstring_view ws) const
{
  std::unique_ptr<WCHAR[]> buf(new WCHAR[ws.size() + 5]);
  lstrcpynW(buf.get(), ws.data(), int(ws.size() + 1));
  for (auto p = buf.get(); *++p;) {
    if (*p == '\t') *p = ' ';
  }
  static RECT r { 0, 0, 300, 300 };
  DrawTextW(hDC, buf.get(), -1, &r,
	    DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_MODIFYSTRING);
  return buf.get();
}

/** iconwindow - icon window with system tray
 */
namespace {
  class iconwindow : public appwindow, window::timer {
    icon _icon;
    tooltips _tips;
    std::string _status;
    UINT _tbcmsg;
    bool _trayicon(bool tray);
    void _update();
    void _updatetips();
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l) override;
    void release() override { _trayicon(false); }
    void draw(HDC hDC) override;
    void raised(bool topmost) override { _tips.topmost(topmost); }
    bool popup(menu const& menu, LPARAM pt) override;
    void wakeup(window& source) override;
    void reset(int type);
    void status(std::string const& text);
    void balloon(std::wstring const& text, unsigned sec,
		 std::wstring const& title = {}, int icon = 0);
    int size() const { return _icon.size(); }
  public:
    iconwindow(icon const& icon);
    ~iconwindow() { if (hwnd()) _trayicon(false); }
    void trayicon(bool tray);
    bool intray() const { return !visible(); }
    void transparent(int alpha) { appwindow::transparent(alpha, ICON_BKGC); }
  };
}

bool
iconwindow::_trayicon(bool tray)
{
  if (NOTIFYICONDATA ni { sizeof(ni), hwnd() }; tray) {
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
    NOTIFYICONDATA ni { sizeof(ni), hwnd() };
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
    NOTIFYICONDATA ni { sizeof(ni), hwnd() };
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
    [[fallthrough]];
  case WM_MOUSELEAVE:
    _tips.reset(hascursor());
    break;
  case WM_USER: // from tray icon
    switch (UINT(l)) {
    case WM_CONTEXTMENU: m = WM_CONTEXTMENU; break;
    case NIN_SELECT: m = WM_LBUTTONDOWN; break;
    case NIN_KEYSELECT: m = WM_LBUTTONDBLCLK; break;
    default: return 0;
    }
    if (POINT pt; GetCursorPos(&pt)) {
      foreground();
      PostMessage(hwnd(), m, 0, MAKELPARAM(pt.x, pt.y));
    }
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
  auto br = CreateSolidBrush(ICON_BKGC);
  DrawIconEx(hDC, r.left, r.top, _icon, r.right, r.bottom, 0, br, DI_NORMAL);
  DeleteObject(HGDIOBJ(br));
}

bool
iconwindow::popup(menu const& menu, LPARAM pt)
{
  auto t = appwindow::popup(menu, pt);
  if (!t && intray()) {
    NOTIFYICONDATA ni { sizeof(ni), hwnd() };
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
iconwindow::status(std::string const& text)
{
  _status = text;
  _tips.clearballoon();
  _updatetips();
}

void
iconwindow::balloon(std::wstring const& text, unsigned sec,
		    std::wstring const& title, int icon)
{
  if (intray()) {
    NOTIFYICONDATAW ni { sizeof(ni), hwnd() };
    ni.uFlags = NIF_INFO;
    auto n = int(text.size());
    if (constexpr auto sz = int(sizeof(ni.szInfo) / sizeof(ni.szInfo[0])); n >= sz) {
      n = sz;
      while (n-- && text[n] != '\n') continue;
      if (n < 0) n = sz - 1;
    }
    lstrcpynW(ni.szInfo, text.c_str(), n + 1);
    ni.uTimeout = sec * 1000;
    lstrcpynW(ni.szInfoTitle, title.c_str(),
	      sizeof(ni.szInfoTitle) / sizeof(ni.szInfoTitle[0]));
    ni.dwInfoFlags = icon & 3;
    Shell_NotifyIconW(NIM_MODIFY, &ni);
  } else {
    _tips.balloon(text, sec, title, icon);
  }
}

iconwindow::iconwindow(icon const& icon)
  : _icon(icon), _tips(self()), _tbcmsg(RegisterWindowMessage("TaskbarCreated"))
{
  style(WS_POPUP, WS_EX_TOOLWINDOW | WS_EX_LAYERED);
  _icon.reset();
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
    int _subjects;
    struct info {
      mailbox const* mbox;
      bool newer;
      std::wstring msg;
    };
    std::list<info> _info;
    DWORD _last = GetTickCount();
    void _release();
    void _updateInfo(mailbox const** mboxes = {});
    static icon _icon();
  protected:
    LRESULT dispatch(UINT m, WPARAM w, LPARAM l) override;
    void release() override;
    void wakeup(window& source) override;
    void status(bool fetched, int recent, int unseen);
    using iconwindow::status;
    unsigned balloonsec() const noexcept { return _balloon ? _balloon : 10; }
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
    MONITORINFO info { sizeof(info) };
    GetMonitorInfo(MonitorFromWindow(hwnd(), MONITOR_DEFAULTTONEAREST), &info);
    RECT dt = info.rcMonitor;
    RECT r = bounds();
    setting::preferences("mascot")
      ("position", setting::tuple
       (r.left)(r.top)(dt.right - dt.left)(dt.bottom - dt.top)(topmost()))
      ("tray", intray());
  } catch (...) {}
}

void
mascotwindow::_updateInfo(mailbox const** mboxes)
{
  auto now = GetTickCount();
  if (now - _last > balloonsec() * 1000) _info.clear();
  _last = now;
  for (tooltips::ellipsis ellips; *mboxes;) {
    info t;
    { 
      auto mbox = *mboxes++;
      auto lock = mbox->lock();
      std::wstring msg;
      auto n = mbox->recent();
      if (n > 0 && _balloon) {
	auto const& mails = mbox->mails();
	msg += win32::wstring
	  (win32::exe.textf(ID_TEXT_FETCHED_MAIL, n, mails.size()) +
	   " @ " + mbox->name()) + L'\n';
	auto mail = mails.crbegin();
	for (auto i = min(n, _subjects); i--; ++mail) {
	  msg += ellips(win32::wstring("- " + mail->subject(), CP_UTF8)) + L'\n';
	}
      } else if (n < 0) {
	msg += win32::wstring(win32::exe.text(ID_TEXT_FETCH_ERROR) +
			      " @ " + mbox->name()) + L'\n';
      }
      t = { mbox, n > 0, { msg, 0 } };
    }
    auto p = _info.begin(), e = _info.end();
    while (p != e && p->mbox != t.mbox) ++p;
    if (p != e) *p = t;
    else _info.push_back(t);
  }
}

icon
mascotwindow::_icon()
{
  try {
    int id;
    std::string fn;
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
    [[fallthrough]];
  case WM_LBUTTONDBLCLK:
    execute(_menu);
    return 0;
  case WM_CONTEXTMENU:
    if (l == ~LPARAM(0) && !intray()) {
      auto pt = extent();
      pt.x >>= 1, pt.y >>= 1;
      ClientToScreen(hwnd(), &pt);
      l = MAKELPARAM(pt.x, pt.y);
    }
    popup(_menu, l);
    return 0;
  case WM_APP: // broadcast
    if (l) _updateInfo(reinterpret_cast<mailbox const**>(l));
    ReplyMessage(0);
    status(l != 0, LOWORD(w), HIWORD(w));
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
mascotwindow::wakeup(window& source)
{
  iconwindow::wakeup(source);
  if (!_info.empty() && GetTickCount() - _last > balloonsec() * 1000) _info.clear();
}

void
mascotwindow::status(bool fetched, int recent, int unseen)
{
  if (fetched) {
    LOG("Recent: " << recent << ", Unseen: " << unseen << std::endl);
    status(win32::exe.textf(ID_TEXT_FETCHED_MAIL, recent, unseen));
    auto newer = false;
    std::wstring msg;
    for (auto& info : _info) {
      newer = newer || info.newer;
      msg += info.msg;
    }
    if (!msg.empty()) {
      msg.erase(msg.size() - 1);
      auto title = win32::wstring(win32::exe.text(newer ? ID_TEXT_BALLOON_TITLE :
						  ID_TEXT_BALLOON_ERROR));
      balloon(msg, balloonsec(), title, newer ? NIIF_INFO : NIIF_ERROR);
    }
    reset(!unseen + 1);
  } else {
    status(win32::exe.text(ID_TEXT_FETCHING));
    reset(0);
  }
}

mascotwindow::mascotwindow()
  : iconwindow(_icon()), _menu(MAKEINTRESOURCE(1))
{
  auto prefs = setting::preferences();
  int icon, transparency;
  prefs["icon"](icon = size())(transparency = 0);
  if (!icon) icon = GetSystemMetrics(SM_CXICON);
  prefs["balloon"](_balloon = 10)(_subjects = 0);

  prefs = setting::preferences("mascot");
  RECT dt;
  GetWindowRect(GetDesktopWindow(), &dt);
  dt.right -= dt.left, dt.bottom -= dt.top;
  RECT r { dt.right - icon, dt.top, dt.right, dt.bottom };
  int raise, tray;
  prefs["position"](r.left)(r.top)(r.right)(r.bottom)(raise = 0);
  prefs["tray"](tray = 0);
  RECT rt { r.left, r.top, r.left + icon, r.top + icon };
  MONITORINFO info { sizeof(info) };
  GetMonitorInfo(MonitorFromRect(&rt, MONITOR_DEFAULTTONEAREST), &info);
  dt = info.rcMonitor;
  dt.right -= dt.left, dt.bottom -= dt.top;
  r.left = dt.left + MulDiv(r.left - dt.left, dt.right, r.right);
  r.top = dt.top + MulDiv(r.top - dt.top, dt.bottom, r.bottom);
  r.right = r.left + icon;
  r.bottom = r.top + icon;
  move(adjust(r, info.rcMonitor, icon / 4));
  transparent(255 - 255 * transparency / 100);
  topmost(raise != 0);
  trayicon(tray != 0);
}

namespace cmd {
  struct trayicon : window::command {
    void execute(window& source) override
    { ((mascotwindow&)source).trayicon(!((mascotwindow&)source).intray()); }
    UINT state(window& source) override
    { return ((mascotwindow&)source).intray() ? MFS_CHECKED : 0; }
  };

  struct alwaysontop : window::command {
    void execute(window& source) override
    { source.topmost(!source.topmost()); }
    UINT state(window& source) override {
      return ((mascotwindow&)source).intray() ?
	MFS_DISABLED : source.topmost() ? MFS_CHECKED : 0;
    }
  };

  struct about : window::command {
    about() : window::command(-1001) {}
    void execute(window& source) override {
      ((mascotwindow&)source).balloon
	(win32::wstring(win32::exe.text(ID_TEXT_ABOUT)), 10,
	 win32::wstring(win32::exe.text(ID_TEXT_VERSION)));
    }
  };
}

window*
mascot()
{
  std::unique_ptr<mascotwindow> w(new mascotwindow);
  w->addcmd(ID_MENU_TRAYICON, new cmd::trayicon);
  w->addcmd(ID_MENU_ALWAYSONTOP, new cmd::alwaysontop);
  w->addcmd(ID_MENU_ABOUT, new cmd::about);
  if (std::string(setting::preferences("mascot")["tray"]).empty()) {
    w->execute(ID_MENU_ABOUT);
  }
  return w.release();
}
