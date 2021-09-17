/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <exception>
#include <list>
#include <string>
#include <windows.h>
#include <mlang.h>

class win32 {
  using _dtfn = decltype(&GetDateFormat);
  static std::string _datetime(time_t utc, DWORD flags, _dtfn fn);
public:
  win32(LPCSTR mutex = {});

  static std::string string(std::wstring_view ws, UINT cp = CP_ACP);
  static std::wstring wstring(std::string_view s, UINT cp = CP_ACP);

  static std::string profile(LPCSTR section, LPCSTR key, LPCSTR file);
  static void profile(LPCSTR section, LPCSTR key, LPCSTR value, LPCSTR file);
  static std::string xenv(std::string const& s);
  static std::string date(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetDateFormat); }
  static std::string time(time_t utc, DWORD flags = 0)
  { return _datetime(utc, flags, GetTimeFormat); }
  static HANDLE shell(std::string_view cmd, unsigned flags = 0);
public:
  // module - module(EXE/DLL) handler
  class module {
    HMODULE _h = {};
  public:
    constexpr module() noexcept {}
    constexpr module(HMODULE h) noexcept : _h(h) {}
    constexpr operator HMODULE() const noexcept { return _h; }
    FARPROC operator()(LPCSTR name) const;
    FARPROC operator()(LPCSTR name, FARPROC def) const;
  public:
    std::string text(UINT id) const;
    std::string textf(UINT id, ...) const;
    std::list<std::string> texts(UINT id) const;
    void* resource(LPCSTR type, LPCSTR name) const;
  };
  static module const exe;

  // dll - DLL loader
  class dll : public module {
  public:
    dll(LPCSTR file, DWORD flags = 0) noexcept
      : module(LoadLibraryEx(file, {}, flags)) {}
    dll(dll const&) = delete;
    ~dll() { *this && FreeLibrary(*this); }
    dll& operator=(dll const&) = delete;
  };

  // find - find files with the path
  class find : public WIN32_FIND_DATA {
    HANDLE _h;
    auto _fd() { return this; }
    void _close() { FindClose(_h), _h = INVALID_HANDLE_VALUE; }
  public:
    find(LPCSTR path) noexcept : _h(FindFirstFile(path, _fd())) {}
    find(std::string const& path) noexcept : find(path.c_str()) {}
    ~find() { if (_h != INVALID_HANDLE_VALUE) _close(); }
    operator bool() const noexcept { return _h != INVALID_HANDLE_VALUE; }
    void next() noexcept { if (!FindNextFile(_h, this)) _close(); }
  };

  // u8conv - UTF-8 converter
  class u8conv {
    static win32::dll const _dll;
    static decltype(&ConvertINetMultiByteToUnicode) const _mb2u;
    UINT _codepage = 0;
    DWORD _mode = 0;
  public:
    u8conv& codepage(UINT codepage) noexcept;
    u8conv& reset() noexcept { _mode = 0; return *this; }
    explicit operator bool() const noexcept { return _codepage != 0; }
    std::string operator()(std::string const& text);
  };

  // error - exception type
  class error : public std::exception {
    std::string _msg;
  public:
    error() : _msg(emsg()) {}
    error(std::string const& msg) : _msg(msg) {}
    char const* what() const noexcept override { return _msg.c_str(); }
    static std::string emsg();
  };
  template<class T> static T valid(T h) { if (h) return h; throw error(); }
};
