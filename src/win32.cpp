/*
 * Copyright (C) 2009-2012 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "win32.h"
#include <cassert>
#include <shlwapi.h>
#include <process.h>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of the class win32
 */
win32::win32(LPCSTR mutex)
{
  if (mutex &&
      (!CreateMutex(NULL, TRUE, mutex) ||
       GetLastError() == ERROR_ALREADY_EXISTS)) throw error();
}

string
win32::profile(LPCSTR section, LPCSTR key, LPCSTR file)
{
  if (!file) return string();
  textbuf<char> buf;
  DWORD size = 0;
  for (DWORD n = 0; n <= size + 2;) {
    n += 256;
    size = GetPrivateProfileString(section, key, "", buf(n), n, file);
  }
  size_t i = size && key ? strspn(buf.data, " \t") : 0;
  return string(buf.data + i, size - i);
}

void
win32::profile(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
{
  file && WritePrivateProfileString(section, key, value, file);
}

string
win32::xenv(const string& s)
{
  textbuf<char> buf;
  DWORD n = static_cast<DWORD>(s.size() + 1);
  n = ExpandEnvironmentStrings(s.c_str(), buf(n), n);
  if (n > s.size() + 1) ExpandEnvironmentStrings(s.c_str(), buf(n), n);
  return buf.data;
}

string
win32::_datetime(time_t utc, DWORD flags, _dtfn fn)
{
  SYSTEMTIME st;
  { // convert a time_t value to SYSTEMTIME format.
    LONGLONG ll = Int32x32To64(utc, 10000000) + 116444736000000000LL;
    FILETIME ft = { DWORD(ll), DWORD(ll >> 32) }, lt;
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
  }
  textbuf<char> buf;
  int n = fn(LOCALE_USER_DEFAULT, flags, &st, NULL, NULL, 0);
  return n && fn(LOCALE_USER_DEFAULT, flags, &st, NULL, buf(n), n) ?
    string(buf.data, n - 1) : string();
}

HANDLE
win32::shell(const string& cmd, unsigned flags)
{
  assert(!(flags & ~(SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT |
		     SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS)));
  textbuf<char> tmp(cmd.size() + 1);
  lstrcpyA(tmp.data, cmd.c_str());
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

#ifdef _MT
HANDLE
win32::thread(void (*func)(void*), void* param)
{
  class _thread {
    void (*_func)(void*);
    void* _param;
    HANDLE _event;
    static unsigned __stdcall _routine(void* param)
    {
      _thread th = *reinterpret_cast<_thread*>(param);
      SetEvent(th._event);
      try { th._func(th._param); } catch (...) {}
      return 0;
    }
  public:
    _thread(void (*func)(void*)) : _func(func) {}
    HANDLE operator()(void* param)
    {
      _param = param;
      _event = valid(CreateEvent(NULL, FALSE, FALSE, NULL));
      uintptr_t p = _beginthreadex(NULL, 0, _routine, (void*)this, 0, NULL);
      if (p) WaitForSingleObject(_event, INFINITE);
      CloseHandle(_event);
      return HANDLE(p);
    }
  };

  return valid(_thread(func)(param));
}
#endif

#ifdef _DEBUG
#include <tlhelp32.h>
#include <malloc.h>

size_t
win32::cheapsize()
{
  _heapmin();
  size_t size = 0;
  _HEAPINFO hi = { NULL };
  for (;;) {
    switch (_heapwalk(&hi)) {
    case _HEAPOK:
      if (hi._useflag == _USEDENTRY) size += hi._size;
      break;
    case _HEAPEND:
      return size;
    default:
      LOG("cheapsize: BAD HEAP!" << endl);
      return size;
    }
  }
}

size_t
win32::heapsize()
{
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPHEAPLIST, 0);
  if (h == HANDLE(-1)) return 0;
  size_t size = 0;
  HEAPLIST32 hl = { sizeof(HEAPLIST32) };
  if (Heap32ListFirst(h, &hl)) {
    do {
      HEAPENTRY32 he = { sizeof(HEAPENTRY32) };
      if (Heap32First(&he, hl.th32ProcessID, hl.th32HeapID)) {
	do {
	  if (!(he.dwFlags & LF32_FREE)) size += he.dwBlockSize;
	} while (Heap32Next(&he));
      }
    } while (Heap32ListNext(h, &hl));
  }
  CloseHandle(h);
  return size;
}
#endif

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
  textbuf<char> buf;
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
  textbuf<char> buf;
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

const win32::module win32::exe(GetModuleHandle(NULL));

/*
 * Functions of the class win32::wstr
 */
LPWSTR
win32::wstr::_new(LPCSTR s, UINT cp)
{
  int size = MultiByteToWideChar(cp, 0, s, -1, NULL, 0);
  if (!size) return NULL;
  LPWSTR ws = new WCHAR[size];
  MultiByteToWideChar(cp, 0, s, -1, ws, size);
  return ws;
}

win32::wstr&
win32::wstr::operator=(LPCWSTR ws)
{
  LPWSTR data = ws ? lstrcpyW(new WCHAR[lstrlenW(ws) + 1], ws) : NULL;
  delete [] _data;
  _data = data;
  return *this;
}

win32::wstr&
win32::wstr::operator+=(LPCWSTR ws)
{
  size_t n = ws ? lstrlenW(ws) : 0;
  if (!n) return *this;
  size_t l = _data ? lstrlenW(_data) : 0;
  LPWSTR data = new WCHAR[l + n + 1];
  if (l) lstrcpyW(data, _data);
  lstrcpyW(data + l, ws);
  delete [] _data;
  _data = data;
  return *this;
}

string
win32::wstr::mbstr(LPCWSTR ws, UINT cp)
{
  if (!ws) return string();
  textbuf<char> buf;
  int n = WideCharToMultiByte(cp, 0, ws, -1, NULL, 0, NULL, NULL);
  return n && WideCharToMultiByte(cp, 0, ws, -1, buf(n), n, NULL, NULL) ?
    string(buf.data, n - 1) : string();
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
