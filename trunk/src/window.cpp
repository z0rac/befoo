/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define _WIN32_IE 0x0300
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

/*
 * Functions of the class window
 */
window::window(LPCSTR classname, LPCSTR menu, HWND owner)
  : _hwnd(_new(classname, menu, owner))
{
  _initialize();
}

window::window(LPCSTR classname, const window& parent, int id)
  : _hwnd(_new(classname, parent, id))
{
  _initialize();
}

window::~window()
{
  if (_hwnd) _release(!child());
}

int
window::eventloop()
{
  for (;;) {
    MSG msg;
    switch (GetMessage(&msg, NULL, 0, 0)) {
    case 0: return int(msg.wParam);
    case -1: continue;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
}

void
window::broadcast(UINT m, WPARAM w, LPARAM l)
{
  struct _enum {
    UINT m;
    WPARAM w;
    LPARAM l;
    DWORD id;
    static BOOL CALLBACK proc(HWND h, LPARAM l)
    {
      DWORD id;
      GetWindowThreadProcessId(h, &id);
      _enum* p = reinterpret_cast<_enum*>(l);
      if (id == p->id) SendMessage(h, p->m, p->w, p->l);
      return TRUE;
    }
  } enums = { m, w, l, GetCurrentProcessId() };
  EnumWindows(_enum::proc, LPARAM(&enums));
}

bool
window::child() const
{
  assert(_hwnd);
  return (GetWindowLong(_hwnd, GWL_STYLE) & WS_CHILD) != 0;
}

void
window::close() const
{
  assert(_hwnd && !child());
  PostMessage(_hwnd, WM_CLOSE, 0, 0);
}

void
window::show(bool show, bool active) const
{
  assert(_hwnd);
  ShowWindow(_hwnd, show ? (active ? SW_SHOW : SW_SHOWNA) : SW_HIDE);
}

void
window::foreground(bool force) const
{
  assert(_hwnd && !child());

  if (IsIconic(_hwnd)) ShowWindow(_hwnd, SW_RESTORE);

  if (force) {
    DWORD tid = GetWindowThreadProcessId(_hwnd, NULL);
    DWORD fore = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    if (tid != fore && AttachThreadInput(tid, fore, TRUE)) {
      DWORD save = 0;
      SetActiveWindow(_hwnd);
      SystemParametersInfo(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &save, 0);
      SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, NULL, 0);
      SetForegroundWindow(_hwnd);
      SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, LPVOID(save), 0);
      AttachThreadInput(tid, fore, FALSE);
      return;
    }
  }
  SetForegroundWindow(_hwnd);
}

bool
window::topmost() const
{
  assert(_hwnd);
  return (GetWindowLong(_hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

void
window::topmost(bool topmost)
{
  assert(_hwnd);
  SetWindowPos(_hwnd, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
	       0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void
window::move(int x, int y, int w, int h) const
{
  assert(_hwnd);
  MoveWindow(_hwnd, x, y, w, h, TRUE);
}

void
window::move(const RECT& r) const
{
  assert(_hwnd);
  MoveWindow(_hwnd, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

bool
window::hascursor(bool child) const
{
  assert(_hwnd);
  POINT pt;
  if (!GetCursorPos(&pt)) return false;
  HWND h = WindowFromPoint(pt);
  return h == _hwnd || child && IsChild(_hwnd, h);
}

POINT
window::extent() const
{
  assert(_hwnd);
  RECT r;
  GetClientRect(_hwnd, &r);
  return (POINT&)r.right;
}

RECT
window::bounds() const
{
  assert(_hwnd);
  RECT r;
  GetWindowRect(_hwnd, &r);
  return r;
}

HWND
window::_new(LPCSTR classname, LPCSTR menu, HWND owner)
{
  HMENU m = menu ? win32::valid(LoadMenu(win32::exe, menu)) : NULL;
  return win32::valid(CreateWindow(classname, NULL, WS_OVERLAPPED,
				   CW_USEDEFAULT, CW_USEDEFAULT,
				   CW_USEDEFAULT, CW_USEDEFAULT,
				   owner, m, win32::exe, NULL));
}

HWND
window::_new(LPCSTR classname, const window& parent, int id)
{
  return win32::valid(CreateWindow(classname, NULL, WS_CHILD,
				   0, 0, 1, 1, parent.hwnd(), HMENU(id),
				   win32::exe, NULL));
}

void
window::_initialize()
{
  _callback = WNDPROC(GetWindowLongPtr(_hwnd, GWLP_WNDPROC));
  SetWindowLongPtr(_hwnd, GWLP_USERDATA, LONG_PTR(this));
  SetWindowLongPtr(_hwnd, GWLP_WNDPROC, LONG_PTR(_wndproc));
}

void
window::_release(bool destroy)
{
  assert(_hwnd);
  SetWindowLongPtr(_hwnd, GWLP_WNDPROC, LONG_PTR(_callback));
  SetWindowLongPtr(_hwnd, GWLP_USERDATA, 0);
  if (destroy) DestroyWindow(_hwnd);
  _hwnd = NULL;
}

LRESULT CALLBACK
window::_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  LRESULT rc = 0;
  window* wp = reinterpret_cast<window*>(GetWindowLongPtr(h, GWLP_USERDATA));
  if (wp) {
    try {
      rc = wp->dispatch(m, w, l);
    } catch (const exception& DBG(e)) {
      LOG(e.what() << endl);
    } catch (...) {
      LOG("Unknown exception." << endl);
    }
    if (m == WM_DESTROY) wp->_release();
  }
  return rc;
}

void
window::_updatemenu(HMENU h)
{
#define STATUS (MFS_DISABLED | MFS_CHECKED | MFS_HILITE)
  for (int i = GetMenuItemCount(h); --i >= 0;) {
    MENUITEMINFO info = { sizeof(MENUITEMINFO) };
    info.fMask = MIIM_STATE | MIIM_ID;
    if (!GetMenuItemInfo(h, i, TRUE, &info)) continue;
    cmdmap::const_iterator p = _cmdmap.begin();
    while (p != _cmdmap.end() && p->first != int(info.wID)) ++p;
    if (p == _cmdmap.end()) continue;
    info.fState &= ~STATUS;
    info.fState |= p->second->state(*this) & STATUS;
    SetMenuItemInfo(h, i, TRUE, &info);
  }
#undef STATUS
}

void
window::style(DWORD style, DWORD ex) const
{
  assert(_hwnd);
  style &= ~(WS_CHILD | WS_VISIBLE);
  style |= GetWindowLong(_hwnd, GWL_STYLE) & (WS_CHILD | WS_VISIBLE);
  SetWindowLong(_hwnd, GWL_STYLE, style);
  if (ex) SetWindowLong(_hwnd, GWL_EXSTYLE, ex);
}

LRESULT
window::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_DESTROY:
    release();
    break;
  case WM_SIZE:
    resize(LOWORD(l), HIWORD(l));
    break;
  case WM_NOTIFY:
    return notify(w, l);
  case WM_COMMAND:
    if (!GET_WM_COMMAND_HWND(w, l)) execute(GET_WM_COMMAND_CMD(w,l));
    break;
  }
  return CallWindowProc(_callback, _hwnd, m, w, l);
}

LRESULT
window::notify(WPARAM w, LPARAM l)
{
  return CallWindowProc(_callback, _hwnd, WM_NOTIFY, w, l);
}

void
window::execute(int id)
{
  cmdmap::const_iterator p = _cmdmap.begin();
  while (p != _cmdmap.end() && p->first != id) ++p;
  if (p != _cmdmap.end() && !(p->second->state(*this) & MFS_DISABLED)) {
    p->second->execute(*this);
  }
}

void
window::addcmd(int id, cmdp cmd)
{
  cmdmap::iterator p = _cmdmap.begin();
  while (p != _cmdmap.end() && p->first != id) ++p;
  if (p != _cmdmap.end()) p->second = cmd;
  else _cmdmap.push_back(pair<int, cmdp>(id, cmd));
}

void
window::execute(const menu& menu)
{
  _updatemenu(menu);
  UINT cmd = GetMenuDefaultItem(menu, FALSE, GMDI_GOINTOPOPUPS);
  if (cmd != UINT(-1)) {
    PostMessage(_hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(0, 0, cmd));
  }
}

bool
window::popup(const menu& menu, DWORD pt)
{
  _updatemenu(menu);
  SetForegroundWindow(_hwnd);
  UINT cmd = UINT(TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
				 GET_X_LPARAM(pt), GET_Y_LPARAM(pt),
				 0, _hwnd, NULL));
  PostMessage(_hwnd, WM_NULL, 0, 0);
  if (!cmd) return false;
  PostMessage(_hwnd, WM_COMMAND, GET_WM_COMMAND_MPS(0, 0, cmd));
  return true;
}

/*
 * Functions of the class window::commctrl
 */
window::commctrl::commctrl(DWORD icc)
{
  INITCOMMONCONTROLSEX icce = { sizeof(INITCOMMONCONTROLSEX), icc };
  InitCommonControlsEx(&icce);
}

/*
 * Functions of the class window::menu
 */
window::menu::menu(LPCSTR name, int pos)
  : _h(NULL)
{
  HMENU bar = win32::valid(LoadMenu(win32::exe, name));
  HMENU h = GetSubMenu(bar, pos);
  if (h && !RemoveMenu(bar, pos, MF_BYPOSITION)) h = NULL;
  DestroyMenu(bar);
  _h = win32::valid(h);
}

/*
 * Functions of the class window::timer
 */
VOID CALLBACK
window::timer::_callback(HWND hwnd, UINT, UINT_PTR id, DWORD ticks)
{
  timer& tm = *reinterpret_cast<timer*>(id);
  if (tm._elapse) {
    ticks -= tm._start;
    if (ticks < tm._elapse) {
      SetTimer(hwnd, id, tm._elapse - ticks, _callback);
    } else {
      tm._elapse = 0;
      tm.wakeup(*reinterpret_cast<window*>
		(GetWindowLongPtr(hwnd, GWLP_USERDATA)));
    }
  }
}

void
window::timer::operator()(HWND hwnd, UINT ms)
{
  _elapse = ms;
  if (ms) {
    _start = GetTickCount();
    SetTimer(hwnd, UINT_PTR(this), ms, _callback);
  } else {
    KillTimer(hwnd, UINT_PTR(this));
  }
}

/*
 * Functions of the class appwindow
 */
LPCSTR
appwindow::_classname()
{
  static const char name[] = "appwindow";
  WNDCLASSEX wc = { sizeof(WNDCLASSEX) };
  if (!GetClassInfoEx(win32::exe, name, &wc)) {
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = win32::exe;
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(0));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = name;
    wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    win32::valid(RegisterClassEx(&wc));
  }
  return name;
}

LRESULT
appwindow::dispatch(UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_PAINT:
    {
      PAINTSTRUCT ps;
      draw(BeginPaint(hwnd(), &ps));
      EndPaint(hwnd(), &ps);
    }
    return 0;
  case WM_ERASEBKGND:
    erase(HDC(w));
    return TRUE;
  case WM_GETMINMAXINFO:
    limit(LPMINMAXINFO(l));
    return 0;
  }
  return window::dispatch(m, w, l);
}

LRESULT
appwindow::notify(WPARAM w, LPARAM l)
{
  return LPNMHDR(l)->hwndFrom == hwnd() ? 0 :
    SendMessage(LPNMHDR(l)->hwndFrom, WM_NOTIFY, w, l);
}

const RECT&
appwindow::adjust(RECT& bounds, int border) const
{
  RECT r;
  GetWindowRect(GetDesktopWindow(), &r);
  InflateRect(&r, -border, -border);
  int w = bounds.right - bounds.left;
  int h = bounds.bottom - bounds.top;
  if (bounds.right < r.left) {
    bounds.left = r.left - w, bounds.right = r.left;
  } else if (bounds.left > r.right) {
    bounds.left = r.right, bounds.right = r.right + w;
  }
  if (bounds.bottom < r.top) {
    bounds.top = r.top - h, bounds.bottom = r.top;
  } else if (bounds.top > r.bottom) {
    bounds.top = r.bottom, bounds.bottom = r.bottom + h;
  }
  return bounds;
}
