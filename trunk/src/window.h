#ifndef H_WINDOW /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009-2012 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_WINDOW

#include <list>
#include <memory>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

using namespace std;

class window {
  HWND _hwnd;
  WNDPROC _callback;
  HWND _new(LPCSTR classname, LPCSTR menu, HWND owner);
  HWND _new(LPCSTR classname, const window& parent, int id);
  void _initialize();
  void _release(bool destroy = false);
  static LRESULT CALLBACK _wndproc(HWND h, UINT m, WPARAM w, LPARAM l);
  void _updatemenu(HMENU h);
protected:
  const window& self() const { return *this; }
  void style(DWORD style, DWORD ex = 0) const;
  virtual LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
  virtual void release() {}
  virtual void resize(int, int) {}
  virtual LRESULT notify(WPARAM w, LPARAM l);
  virtual bool callback(LPMEASUREITEMSTRUCT misp);
  virtual bool callback(LPDRAWITEMSTRUCT disp);

  struct commctrl {
    commctrl(DWORD icc);
  };

  class menu {
    HMENU _h;
  public:
    menu(LPCSTR name, int pos = 0);
    ~menu() { DestroyMenu(_h); }
    operator HMENU() const { return _h; }
  };
  void execute(const menu& menu);
  virtual bool popup(const menu& menu, LPARAM pt);
public:
  window(LPCSTR classname, LPCSTR menu = NULL, HWND owner = NULL);
  window(LPCSTR classname, const window& parent, int id = -1);
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
  void move(const RECT& r) const;
  void invalidate() const { InvalidateRect(_hwnd, NULL, TRUE); }
  bool hascursor(bool child = true) const;
  POINT extent() const;
  RECT bounds() const;
public:
  class command {
    command(const command&); void operator=(const command&); // disable to copy
  public:
    const int icon;
    command(int icon = 0) : icon(icon) {}
    virtual ~command() {}
    virtual void execute(window& source) = 0;
    virtual UINT state(window&) { return 0; }
  };
  struct cmdp : public auto_ptr<command> {
    typedef auto_ptr<command> super;
    cmdp(command* cmd = NULL) : super(cmd) {}
    cmdp(const cmdp& cmd) : super(const_cast<cmdp&>(cmd)) {}
    cmdp& operator=(const cmdp& cmd)
    { super::operator=(const_cast<cmdp&>(cmd)); return *this; }
  };
  void addcmd(int id, cmdp cmd);
  virtual void execute(int id);
private:
  typedef list< pair<int, cmdp> > cmdmap;
  cmdmap _cmdmap;
  command* _cmd(int id);
public:
  class timer {
    UINT _elapse;
    DWORD _start;
    static VOID CALLBACK _callback(HWND hwnd, UINT, UINT_PTR id, DWORD ticks);
  public:
    timer() : _elapse(0) {}
    virtual ~timer() {}
    void operator()(HWND hwnd, UINT ms);
    virtual void wakeup(window& source) = 0;
  };
  void settimer(timer& tm, UINT ms) const { tm(_hwnd, ms); }
};

class appwindow : public window {
  static LPCSTR _classname();
protected:
  LRESULT dispatch(UINT m, WPARAM w, LPARAM l);
  LRESULT notify(WPARAM w, LPARAM l);
  virtual void draw(HDC) {}
  virtual void erase(HDC) {}
  virtual void limit(LPMINMAXINFO) {}
  virtual void raised(bool) {}
  const RECT& adjust(RECT& bounds, int border = 8) const;
  const RECT& adjust(RECT& bounds, const RECT& monitor, int border = 8) const;
public:
  appwindow(LPCSTR menu = NULL, HWND owner = NULL)
    : window(_classname(), menu, owner) {}
  appwindow(const window& parent, int id = -1)
    : window(_classname(), parent, id) {}
};

#endif
