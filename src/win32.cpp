/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of the class win32
 */
win32::win32(LPCSTR mutex)
{
  if (mutex && (!CreateMutex({}, TRUE, mutex) ||
		GetLastError() == ERROR_ALREADY_EXISTS)) throw error();
}

std::string
win32::string(std::wstring_view ws, UINT cp)
{
  auto n = WideCharToMultiByte(cp, 0, ws.data(), int(ws.size()), {}, 0, {}, {});
  if (!n) return {};
  std::unique_ptr<char[]> s(new char[n]);
  n = WideCharToMultiByte(cp, 0, ws.data(), int(ws.size()), s.get(), n, {}, {});
  return std::string(s.get(), n);
}

std::wstring
win32::wstring(std::string_view s, UINT cp)
{
  auto n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), {}, 0);
  if (!n) return {};
  std::unique_ptr<WCHAR[]> ws(new WCHAR[n]);
  n = MultiByteToWideChar(cp, 0, s.data(), int(s.size()), ws.get(), n);
  return std::wstring(ws.get(), n);
}

std::string
win32::profile(LPCSTR section, LPCSTR key, LPCSTR file)
{
  if (!file) return {};
  std::unique_ptr<char[]> buf;
  DWORD size = 0;
  for (DWORD n = 0; n <= size + 2;) {
    n += 256;
    buf = std::unique_ptr<char[]>(new char[n]);
    size = GetPrivateProfileString(section, key, "", buf.get(), n, file);
  }
  size_t i = size && key ? strspn(buf.get(), " \t") : 0;
  return { buf.get() + i, size - i };
}

void
win32::profile(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file)
{
  file && WritePrivateProfileString(section, key, value, file);
}

std::string
win32::xenv(std::string const& s)
{
  auto n = static_cast<DWORD>(s.size() + 1);
  std::unique_ptr<char[]> buf(new char[n]);
  n = ExpandEnvironmentStrings(s.c_str(), buf.get(), n);
  if (n > s.size() + 1) {
    buf = std::unique_ptr<char[]>(new char[n]);
    ExpandEnvironmentStrings(s.c_str(), buf.get(), n);
  }
  return buf.get();
}

std::string
win32::_datetime(time_t utc, DWORD flags, _dtfn fn)
{
  SYSTEMTIME st;
  { // convert a time_t value to SYSTEMTIME format.
    LONGLONG ll = Int32x32To64(utc, 10000000) + 116444736000000000LL;
    FILETIME ft = { DWORD(ll), DWORD(ll >> 32) }, lt;
    FileTimeToLocalFileTime(&ft, &lt);
    FileTimeToSystemTime(&lt, &st);
  }
  if (auto n = fn(LOCALE_USER_DEFAULT, flags, &st, {}, {}, 0); n) {
    std::unique_ptr<char[]> buf(new char[n]);
    if (fn(LOCALE_USER_DEFAULT, flags, &st, {}, buf.get(), n)) {
      return std::string(buf.get(), n - 1);
    }
  }
  return {};
}

HANDLE
win32::shell(std::string_view cmd, unsigned flags)
{
  assert(!(flags & ~(SEE_MASK_CONNECTNETDRV | SEE_MASK_FLAG_DDEWAIT |
		     SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS)));
  std::unique_ptr<char[]> tmp(new char[cmd.size() + 1]);
  lstrcpynA(tmp.get(), cmd.data(), int(cmd.size() + 1));
  PathRemoveArgs(tmp.get());
  size_t i = lstrlen(tmp.get());
  if (i < cmd.size()) i = cmd.find_first_not_of("\t ", i + 1);
  SHELLEXECUTEINFO info = {
    sizeof(info), flags, {}, "open",
    tmp.get(), i < cmd.size() ? tmp.get() + i : nullptr, {}, SW_SHOWNORMAL
  };
  return !ShellExecuteEx(&info) ? nullptr :
    (flags & SEE_MASK_NOCLOSEPROCESS) ? info.hProcess : INVALID_HANDLE_VALUE;
}

#ifdef _DEBUG
#include <tlhelp32.h>
#include <malloc.h>

size_t
win32::cheapsize() noexcept
{
  _heapmin();
  size_t size = 0;
  _HEAPINFO hi {};
  for (;;) {
    switch (_heapwalk(&hi)) {
    case _HEAPOK:
      if (hi._useflag == _USEDENTRY) size += hi._size;
      break;
    case _HEAPEND:
      return size;
    default:
      LOG("cheapsize: BAD HEAP!" << std::endl);
      return size;
    }
  }
}

size_t
win32::heapsize() noexcept
{
  auto h = CreateToolhelp32Snapshot(TH32CS_SNAPHEAPLIST, 0);
  if (h == HANDLE(-1)) return 0;
  size_t size = 0;
  if (HEAPLIST32 hl { sizeof(hl) }; Heap32ListFirst(h, &hl)) {
    do {
      if (HEAPENTRY32 he { sizeof(he) }; Heap32First(&he, hl.th32ProcessID, hl.th32HeapID)) {
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
  auto proc = GetProcAddress(_h, name);
  return proc ? proc : def;
}

std::string
win32::module::text(UINT id) const
{
  std::unique_ptr<char[]> buf;
  int size = 0;
  for (int n = 0; n <= size + 1;) {
    n += 256;
    buf = std::unique_ptr<char[]>(new char[n * 2]);
    size = LoadString(_h, id, buf.get(), n * 2);
  }
  return std::string(buf.get(), size);
}

std::string
win32::module::textf(UINT id, ...) const
{
  auto s = text(id);
  std::unique_ptr<char[]> buf;
  int size = 0;
  for (int n = 0; n <= size + 1;) {
    n += 512;
    buf = std::unique_ptr<char[]>(new char[n]);
    va_list arg;
    va_start(arg, id);
    size = wvnsprintf(buf.get(), n, s.c_str(), arg);
    va_end(arg);
  }
  return size > 0 ? std::string(buf.get(), size) : s;
}

std::list<std::string>
win32::module::texts(UINT id) const
{
  std::list<std::string> result;
  if (auto t = text(id); t.size() > 1) {
    auto delim = *t.rbegin();
    for (size_t i = 0; i < t.size();) {
      auto n = t.find(delim, i);
      result.push_back(t.substr(i, n - i));
      i = n + 1;
    }
  }
  return result;
}

void*
win32::module::resource(LPCSTR type, LPCSTR name) const
{
  auto h = FindResource(_h, name, type);
  return h ? LockResource(LoadResource(_h, h)) : nullptr;
}

win32::module const win32::exe(GetModuleHandle({}));

/*
 * Functions fo the class win32::u8conv
 */
win32::dll const win32::u8conv::_dll("mlang.dll");
decltype(win32::u8conv::_mb2u) win32::u8conv::_mb2u =
  decltype(_mb2u)(_dll("ConvertINetMultiByteToUnicode", {}));

win32::u8conv&
win32::u8conv::codepage(UINT codepage) noexcept
{
  if (codepage && codepage != _codepage) {
    _codepage = codepage, _mode = 0;
  }
  return *this;
}

std::string
win32::u8conv::operator()(std::string const& text)
{
  if (!*this) throw text;
  if (_codepage == CP_UTF8) return text;
  if (!_mb2u) return win32::string(win32::wstring(text, _codepage), CP_UTF8);
  int n = 0;
  if (_mb2u(&_mode, _codepage, text.c_str(), {}, {}, &n) != S_OK) throw text;
  std::unique_ptr<WCHAR[]> buf(new WCHAR[n + 1]);
  _mb2u(&_mode, _codepage, text.c_str(), {}, buf.get(), &n);
  return win32::string(std::wstring_view(buf.get(), n), CP_UTF8);
}

/*
 * Functions of the class win32::error
 */
std::string
win32::error::emsg()
{
  char s[35];
  return std::string("WinAPI error #") + _ultoa(GetLastError(), s, 10);
}
