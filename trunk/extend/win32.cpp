/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "win32.h"
#include <cassert>
#include <shlwapi.h>
#include <process.h>

/*
 * Functions of the class win32
 */
namespace {
  struct textbuf {
    char* data;
    textbuf() : data(NULL) {}
    ~textbuf() { delete [] data; }
    char* operator()(size_t n)
    {
      assert(n);
      delete [] data, data = NULL;
      return data = new char[n];
    }
  };
}

string
win32::profile(LPCSTR section, LPCSTR key, LPCSTR file, LPCSTR def)
{
  if (!def) def = "";
  textbuf buf;
  DWORD size = 0;
  for (DWORD n = 0; n <= size + 2;) {
    n += 256;
    size = GetPrivateProfileString(section, key, def, buf(n), n, file);
  }
  size_t i = size && key ? strspn(buf.data, " \t") : 0;
  return string(buf.data + i, size - i);
}

string
win32::xenv(const string& s)
{
  textbuf buf;
  size_t n = s.size() + 1;
  n = ExpandEnvironmentStrings(s.c_str(), buf(n), n);
  if (n > s.size() + 1) ExpandEnvironmentStrings(s.c_str(), buf(n), n);
  return buf.data;
}

HANDLE
win32::shell(const string& cmd, unsigned flags)
{
  assert(!(flags & ~(SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT |
		     SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS)));
  textbuf tmp;
  lstrcpyA(tmp(cmd.size() + 1), cmd.c_str());
  PathRemoveArgs(tmp.data);
  string::size_type i = lstrlen(tmp.data);
  if (i < cmd.size()) i = cmd.find_first_not_of(" \t", i + 1);
  SHELLEXECUTEINFO info = {
    sizeof(SHELLEXECUTEINFO), flags, NULL, "open",
    tmp.data, i < cmd.size() ? tmp.data + i : NULL, NULL, SW_SHOWNORMAL
  };
  return !ShellExecuteEx(&info) ? NULL :
    (flags & SEE_MASK_NOCLOSEPROCESS) ? info.hProcess : INVALID_HANDLE_VALUE;
}


/*
 * Functions of the class win32::module
 */
FARPROC
win32::module::operator()(LPCSTR name) const
{
  return valid(GetProcAddress(_h, name));
}

FARPROC
win32::module::operator()(LPCSTR name, FARPROC def) const
{
  FARPROC proc = GetProcAddress(_h, name);
  return proc ? proc : def;
}

string
win32::module::text(UINT id) const
{
  textbuf buf;
  int size = 0;
  for (int n = 0; n <= size + 1;) {
    n += 256;
    size = LoadString(_h, id, buf(n * 2), n * 2);
  }
  return string(buf.data, size);
}

string
win32::module::textf(UINT id, ...) const
{
  string s = text(id);
  textbuf buf;
  int size = 0;
  for (int n = 0; n <= size + 1;) {
    n += 512;
    va_list arg;
    va_start(arg, id);
    size = wvnsprintf(buf(n), n, s.c_str(), arg);
    va_end(arg);
  }
  return size > 0 ? string(buf.data, size) : s;
}

list<string>
win32::module::texts(UINT id) const
{
  list<string> result;
  string t = text(id);
  if (t.size() > 1) {
    char delim = *t.rbegin();
    for (string::size_type i = 0; i < t.size();) {
      string::size_type n = t.find_first_of(delim, i);
      result.push_back(t.substr(i, n - i));
      i = n + 1;
    }
  }
  return result;
}

void*
win32::module::resource(LPCSTR type, LPCSTR name) const
{
  HRSRC h = FindResource(_h, name, type);
  return h ? LockResource(LoadResource(_h, h)) : NULL;
}

win32::module win32::instance;

/*
 * Functions of the class win32::dll
 */
win32::dll::dll(LPCSTR file, bool must)
  : module(LoadLibrary(file))
{
  must && valid<HMODULE>(*this);
}

win32::dll::~dll()
{
  if (*this) FreeLibrary(*this);
}

/*
 * Functions of the class win32::wstr
 */
win32::wstr::wstr(const string& s, UINT cp)
  : _data(NULL)
{
  int size = MultiByteToWideChar(cp, 0, s.c_str(), -1, NULL, 0);
  if (size) {
    _data = new WCHAR[size];
    MultiByteToWideChar(cp, 0, s.c_str(), -1, _data, size);
  }
}

const win32::wstr&
win32::wstr::operator=(const wstr& ws)
{
  LPWSTR data = NULL;
  if (ws._data) {
    data = lstrcpyW(new WCHAR[lstrlenW(ws._data) + 1], ws._data);
  }
  delete [] _data;
  _data = data;
  return *this;
}

string
win32::wstr::mbstr(UINT cp) const
{
  if (_data) {
    int size = WideCharToMultiByte(cp, 0, _data, -1, NULL, 0, NULL, NULL);
    if (size) {
      textbuf buf;
      WideCharToMultiByte(cp, 0, _data, -1, buf(size), size, NULL, NULL);
      return string(buf.data, size - 1);
    }
  }
  return string();
}

/*
 * Functions of the class win32::error
 */
string
win32::error::emsg()
{
  char s[35];
  return string("WinAPI error #") + _ultoa(GetLastError(), s, 10);
}
