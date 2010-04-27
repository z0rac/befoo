#ifndef H_WIN32 /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_WIN32

#include <exception>
#include <list>
#include <string>
#include <windows.h>

using namespace std;

class win32 {
  typedef int (WINAPI* _dtfn)(LCID,DWORD,const SYSTEMTIME*,LPCSTR,LPSTR,int);
  static string _datetime(time_t utc, DWORD flags, _dtfn fn);
public:
  win32(LPCSTR mutex = NULL);

  static string profile(LPCSTR section, LPCSTR key, LPCSTR file);
  static void profile(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file);
  static string xenv(const string& s);
  static string date(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetDateFormat); }
  static string time(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetTimeFormat); }
  static HANDLE shell(const string& cmd, unsigned flags = 0);
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
    dll(LPCSTR file) : module(LoadLibrary(file)) {}
    ~dll() { *this && FreeLibrary(*this); }
  };

  // mex - exclusive control
  class mex {
    CRITICAL_SECTION _cs;
    mex(const mex&); void operator=(const mex&); // disable to copy
  public:
    mex() { InitializeCriticalSectionAndSpinCount(&_cs, 4000); }
    ~mex() { DeleteCriticalSection(&_cs); }
  public:
    class lock {
      mex& _m;
      lock(const lock&); void operator=(const lock&);
    public:
      lock(mex& m) : _m(m) { EnterCriticalSection(&m._cs); }
      ~lock() { LeaveCriticalSection(&_m._cs); }
    };
    friend class lock;
  public:
    class trylock {
      mex* _mp;
      trylock(const trylock&); void operator=(const trylock&);
    public:
      trylock(mex& m) : _mp(TryEnterCriticalSection(&m._cs) ? &m : NULL) {}
      ~trylock() { if (_mp) LeaveCriticalSection(&_mp->_cs); }
      operator bool() const { return _mp != NULL; }
    };
    friend class trylock;
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
    string mbstr(UINT cp = GetACP()) const { return mbstr(_data, cp); }
    static string mbstr(LPCWSTR ws, UINT cp = GetACP());
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

#endif
