#ifndef H_WIN32 /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_WIN32

#include <exception>
#include <list>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

using namespace std;

class win32 {
  typedef int (WINAPI* _dtfn)(LCID,DWORD,const SYSTEMTIME*,LPCSTR,LPSTR,int);
  static string _datetime(time_t utc, DWORD flags, _dtfn fn);
public:
  win32(LPCSTR mutex = NULL);

  static string profile(LPCSTR section, LPCSTR key, LPCSTR file, LPCSTR def = NULL);
  static string xenv(const string& s);
  static string date(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetDateFormat); }
  static string time(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetTimeFormat); }
#ifdef _MT
  static HANDLE thread(void (*func)(void*), void* param);
#endif
#ifdef _DEBUG
  static size_t cheapsize();
  static size_t heapsize();
#endif
public:
  // module - module(EXE/DLL) handler
  class module {
    HMODULE _h;
  public:
    module(HMODULE h = NULL) : _h(h) {}
    operator HMODULE() const { return _h; }
    FARPROC operator()(LPCSTR name) const;
    FARPROC operator()(LPCSTR name, FARPROC def) const;
  public:
    string text(UINT id) const;
    string textf(UINT id, ...) const;
    list<string> texts(UINT id) const;
    void* resource(LPCSTR type, LPCSTR name) const;
  };
  static const module exe;

  // dll - DLL loader
  class dll : public module {
    dll(const dll&); void operator=(const dll&); // disable to copy
  public:
    dll(LPCSTR file, bool must = true);
    ~dll();
  };

  // xlock - exclusive control
  class xlock {
    CRITICAL_SECTION _cs;
    xlock(const xlock&); void operator=(const xlock&); // disable to copy
  public:
    xlock() { InitializeCriticalSection(&_cs); }
    ~xlock() { DeleteCriticalSection(&_cs); }
  public:
    class up {
      xlock& _lock;
      up(const up&);
      void operator=(const up&);
    public:
      up(xlock& lock) : _lock(lock) { EnterCriticalSection(&_lock._cs); }
      ~up() { LeaveCriticalSection(&_lock._cs); }
    };
    friend class up;
  };

  // wstr - wide character string
  class wstr {
    LPWSTR _data;
  public:
    wstr() : _data(NULL) {}
    wstr(const wstr& ws) : _data(NULL) { *this = ws; }
    wstr(const string& s, UINT cp = GetACP());
    ~wstr() { delete [] _data; }
    const wstr& operator=(const wstr& ws);
    operator LPCWSTR() const { return _data; }
    size_t size() const { return _data ? lstrlenW(_data) : 0; }
    string mbstr(UINT cp = GetACP()) const;
  };

  // error - exception type
  class error : public exception {
    string _msg;
  public:
    error() : _msg(emsg()) {}
    error(const string& msg) : _msg(msg) {}
    ~error() throw() {}
    const char* what() const throw() { return _msg.c_str(); }
    static string emsg();
  };
  template<typename _Th>
  static _Th valid(_Th h) { if (h) return h; throw error(); }
};

// winsock - winsock handler
class winsock {
  win32::dll _dll;
  typedef int (WSAAPI *_get_t)(const char*, const char*,
			       const struct addrinfo*, struct addrinfo**);
  typedef void (WSAAPI *_free_t)(struct addrinfo*);
  static _get_t _get;
  static _free_t _free;
public:
  winsock();
  ~winsock() { WSACleanup(); }
  static struct addrinfo* getaddrinfo(const string& host, const string& port);
  static void freeaddrinfo(struct addrinfo* info) { _free(info); }
public:
  struct error : public win32::error {
    error(int err = h_errno) : win32::error(emsg(err)) {}
    static string emsg(int err);
  };
};

#endif
