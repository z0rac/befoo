/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <list>
#include <memory>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

class window {
  HWND _hwnd;
  WNDPROC _callback;
  HWND _new(LPCSTR classname, LPCSTR menu, HWND owner);
  HWND _new(LPCSTR classname, window const& parent, int id);
  void _initialize();
  void _release(bool destroy = false);
  static LRESULT CALLBACK _wndproc(HWND h, UINT m, WPARAM w, LPARAM l);
  void _updatemenu(HMENU h);
protected:
  window const& self() const { return *this; }
  void style(DWORD style, DWORD ex = 0) const;
  virtual LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
  virtual void release() {}
  virtual void resize(int, int) {}
  virtual LRESULT notify(WPARAM w, LPARAM l);

  struct commctrl {
    commctrl(DWORD icc);
  };

  class menu {
    HMENU _h = {};
  public:
    menu(LPCSTR name, int pos = 0);
    ~menu() { DestroyMenu(_h); }
    operator HMENU() const { return _h; }
  };
  void execute(menu const& menu);
  virtual bool popup(menu const& menu, LPARAM pt);
public:
  window(LPCSTR classname, LPCSTR menu = {}, HWND owner = {});
  window(LPCSTR classname, window const& parent, int id = -1);
  virtual ~window();
  static int eventloop();
  static void broadcast(UINT m, WPARAM w, LPARAM l);
public:
  HWND hwnd() const { return _hwnd; }
  bool visible() const { return IsWindowVisible(_hwnd) != 0; }
  bool child() const;
  void close(bool root = false) const;
  void show(bool show = true, bool active = true) const;
  void foreground(bool force = false) const;
  bool topmost() const;
  void topmost(bool topmost);
  void transparent(int alpha, COLORREF key = COLORREF(-1));
  void move(int x, int y, int w, int h) const;
  void move(RECT const& r) const;
  void invalidate() const { InvalidateRect(_hwnd, {}, TRUE); }
  bool hascursor(bool child = true) const;
  POINT extent() const;
  RECT bounds() const;
public:
  struct command {
    int const icon;
    command(int icon = 0) : icon(icon) {}
    command(command const&) = delete;
    virtual ~command() {}
    command& operator=(command const&) = delete;
    virtual void execute(window& source) = 0;
    virtual UINT state(window&) { return 0; }
  };
  struct cmdp : public std::shared_ptr<command> {
    using super = std::shared_ptr<command>;
    cmdp(command* cmd = {}) : super(cmd) {}
    cmdp(cmdp const& cmd) : super(cmd) {}
  };
  void addcmd(int id, cmdp cmd);
  virtual void execute(int id);
private:
  using cmdmap = std::list<std::pair<int, cmdp>>;
  cmdmap _cmdmap;
  command* _cmd(int id);
public:
  class timer {
    UINT _elapse = 0;
    DWORD _start;
    static VOID CALLBACK _callback(HWND hwnd, UINT, UINT_PTR id, DWORD ticks);
  public:
    timer() {}
    virtual ~timer() {}
    void operator()(HWND hwnd, UINT ms);
    virtual void wakeup(window& source) = 0;
  };
  void settimer(timer& tm, UINT ms) const { tm(_hwnd, ms); }
};

class appwindow : public window {
  static LPCSTR _classname();
protected:
  LRESULT dispatch(UINT m, WPARAM w, LPARAM l) override;
  LRESULT notify(WPARAM w, LPARAM l) override;
  virtual void draw(HDC) {}
  virtual void erase(HDC) {}
  virtual void limit(LPMINMAXINFO) {}
  virtual void raised(bool) {}
  RECT const& adjust(RECT& bounds, int border = 8) const;
  RECT const& adjust(RECT& bounds, RECT const& monitor, int border = 8) const;
public:
  appwindow(LPCSTR menu = {}, HWND owner = {})
    : window(_classname(), menu, owner) {}
  appwindow(window const& parent, int id = -1)
    : window(_classname(), parent, id) {}
};
