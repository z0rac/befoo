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
#if defined(__MINGW32__) && defined(_MT)
  extern CRITICAL_SECTION __mingwthr_cs;
  static struct mingwthr_cs {
    mingwthr_cs() { InitializeCriticalSection(&__mingwthr_cs); }
    ~mingwthr_cs() { DeleteCriticalSection(&__mingwthr_cs); }
  } mingwthr_cs;
#endif
  if (mutex &&
      (!CreateMutex(NULL, TRUE, mutex) ||
       GetLastError() == ERROR_ALREADY_EXISTS)) throw error();
}

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
  textbuf buf;
  int n = fn(LOCALE_USER_DEFAULT, flags, &st, NULL, NULL, 0);
  if (n && fn(LOCALE_USER_DEFAULT, flags, &st, NULL, buf(n), n)) {
    return buf.data;
  }
  return string();
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

#ifdef _MT
#if defined(__MINGW32__)
extern "C" void __mingwthr_run_key_dtors(void);
#endif

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
#if defined(__MINGW32__)
      __mingwthr_run_key_dtors();
#endif
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
  size_t size = 0;
  HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPHEAPLIST, 0);
  if (h != HANDLE(-1)) {
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
  }
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

const win32::module win32::exe(GetModuleHandle(NULL));

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

/*
 * Functions of the class winsock
 */
namespace {
  int WSAAPI
  _getaddrinfo(const char* node, const char* service,
	       const struct addrinfo* hints, struct addrinfo** res)
  {
    assert(node && service && hints);
    assert(hints->ai_flags == 0 && hints->ai_family == AF_UNSPEC &&
	   hints->ai_socktype == SOCK_STREAM && hints->ai_protocol == 0);

    LOG("Call _getaddrinfo()" << endl);

    struct sockaddr_in sa = { AF_INET };
    struct addrinfo ai = { 0, AF_INET, SOCK_STREAM, IPPROTO_TCP, sizeof(sa) };
    static win32::xlock key;

    { // service to port number
      char* end;
      unsigned n = strtoul(service, &end, 10);
      if (*end || n > 65535) {
	win32::xlock::up lockup(key);
	struct servent* ent = getservbyname(service, "tcp");
	if (!ent) return h_errno;
	sa.sin_port = ent->s_port;
      } else {
	sa.sin_port = htons(static_cast<u_short>(n));
      }
    }

    sa.sin_addr.s_addr = inet_addr(node);
    if (sa.sin_addr.s_addr == INADDR_NONE) {
      win32::xlock::up lockup(key);
      struct hostent* ent = gethostbyname(node);
      if (!ent) return h_errno;
      if (ent->h_addrtype != AF_INET ||
	  ent->h_length != sizeof(sa.sin_addr.s_addr)) return EAI_FAMILY;
      char** hal = ent->h_addr_list;
      int n = 0;
      while (hal && hal[n]) ++n;
      if (n == 0) return EAI_NONAME;

      *res = reinterpret_cast<struct addrinfo*>(new char[(sizeof(ai) + sizeof(sa)) * n]);
      struct addrinfo* ail = *res;
      struct sockaddr_in* sal = reinterpret_cast<struct sockaddr_in*>(*res + n);
      for (int i = 0; i < n; ++i) {
	sa.sin_addr = *reinterpret_cast<struct in_addr*>(hal[i]);
	ai.ai_addr = reinterpret_cast<struct sockaddr*>(&sal[i]);
	ai.ai_next = i + 1 < n ? &ail[i + 1] : NULL;
	ail[i] = ai, sal[i] = sa;
      }
    } else {
      *res = reinterpret_cast<struct addrinfo*>(new char[sizeof(ai) + sizeof(sa)]);
      ai.ai_addr = reinterpret_cast<struct sockaddr*>(*res + 1);
      **res = ai;
      *reinterpret_cast<struct sockaddr_in*>(ai.ai_addr) = sa;
    }
    return 0;
  }

  void WSAAPI
  _freeaddrinfo(struct addrinfo* info)
  {
    LOG("Call _freeaddrinfo()" << endl);
    delete [] reinterpret_cast<char*>(info);
  }
}

winsock::_get_t winsock::_get = NULL;
winsock::_free_t winsock::_free = NULL;

winsock::winsock()
  : _dll("ws2_32.dll", false)
{
  _get = _get_t(_dll("getaddrinfo", FARPROC(_getaddrinfo)));
  _free = _free_t(_dll("freeaddrinfo", FARPROC(_freeaddrinfo)));

  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) throw error();
  LOG("Winsock version: " <<
      int(HIBYTE(wsa.wVersion)) << '.' << int(LOBYTE(wsa.wVersion)) << " <= " <<
      int(HIBYTE(wsa.wHighVersion)) << '.' << int(LOBYTE(wsa.wHighVersion)) << endl);
}

struct addrinfo*
winsock::getaddrinfo(const string& host, const string& port)
{
  struct addrinfo hints = { 0, AF_UNSPEC, SOCK_STREAM };
  struct addrinfo* res;
  int err = _get(host.c_str(), port.c_str(), &hints, &res);
  if (err != 0) throw error(err);
  return res;
}

string
winsock::error::emsg(int err)
{
  char s[35];
  return string("winsock error #") + _ltoa(err, s, 10);
}
