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
#include <windows.h>

using namespace std;

class win32 {
public:
  static string profile(LPCSTR section, LPCSTR key, LPCSTR file, LPCSTR def = NULL);
  static string xenv(const string& s);
  static HANDLE shell(const string& cmd, unsigned flags = 0);
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
  static module instance;

  // dll - DLL loader
  class dll : public module {
    dll(const dll&); void operator=(const dll&); // disable to copy
  public:
    dll(LPCSTR file, bool must = true);
    ~dll();
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

#endif
