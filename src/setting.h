/* -*- mode: c++ -*-
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#pragma once

#include <list>
#include <memory>
#include <string>

class setting {
  struct _str {
    char const* c_str;
    constexpr _str(char const* s = {}) noexcept : c_str(s) {}
    constexpr _str(std::string const& s) noexcept : c_str(s.c_str()) {}
    constexpr operator char const*() const noexcept { return c_str; }
  };
  class _profile;
  class _registory;
  class storage {
  public:
    virtual ~storage() {}
    virtual std::string get(_str key) const = 0;
    virtual void put(_str key, _str value) = 0;
    virtual void erase(_str key) = 0;
    virtual std::list<std::string> keys() const = 0;
  };
  class watch {
  public:
    virtual ~watch() {}
    virtual bool changed() const noexcept = 0;
  };
  class repository {
  public:
    repository() { _rep = this; }
    virtual ~repository() {}
    using _str = setting::_str;
    virtual std::unique_ptr<setting::storage> storage(_str name) const = 0;
    virtual std::list<std::string> storages() const = 0;
    virtual void erase(_str name) = 0;
    virtual char const* invalidchars() const noexcept = 0;
    virtual std::unique_ptr<setting::watch> watch() const = 0;
  };
  static inline repository* _rep = {};
  std::unique_ptr<storage> _st;
  static std::string _cachekey(std::string_view key);
  setting(std::unique_ptr<storage>&& st) { std::swap(_st, st); }
public:
  setting(setting const& s) = delete;
  setting(setting&& s) { std::swap(_st, s._st); }
  setting& operator=(setting const&) = delete;
  setting& operator=(setting&& s) { return std::swap(_st, s._st), *this; }
  static std::unique_ptr<repository> profile(_str path);
  static std::unique_ptr<repository> registory(_str key);
public:
  // tuple - use for separated output parameters.
  // Example:
  //   setting::preferences()("windowpos", setting::tuple(x)(y)(w)(h));
  class tuple : public std::string {
    char _sep;
    tuple& add(_str s);
    static std::string digit(long i);
  public:
    tuple(_str v, char sep = ',') : std::string(v), _sep(sep) {}
    tuple(long v, char sep = ',') : std::string(digit(v)), _sep(sep) {}
    auto& operator()(_str v) { return add(v); }
    auto& operator()(long v) { return add(digit(v)); }
  };
  auto& operator()(_str key, _str value) { return _st->put(key, value), *this; }
  auto& operator()(_str key, long value) { return _st->put(key, tuple(value)), *this; }

  // manip - use for separated input parameters.
  // Examples:
  //   s = setting::preferences()["title"];
  //   setting::preferences()["windowpos"](x)(y)(w)(h);
  class manip : public std::string {
    size_type _next = 0;
    char _sep = ',';
    auto avail() const { return _next < size(); }
    std::string_view next();
    bool next(int& v);
  public:
    manip(std::string const& s) : std::string(s) {}
    manip(std::string&& s) : std::string(s) {}
    auto& operator()() { return next(), *this; }
    manip& operator()(std::string& v);
    template<class T> auto& operator()(T& v)
    { auto i = int(v); next(i), v = T(i); return *this; }
    template<class T, class U> auto& operator()(T& v, U& e)
    { auto i = int(v); e = U(next(i)), v = T(i); return *this; }
    auto& operator()(int& v) { return next(v), *this; }
    template<class T> auto& operator()(int& v, T& e) { return e = T(next(v)), *this; }
    auto& sep(char sep) { return _sep = sep, *this; }
    std::list<std::string> split();
    template<class T> std::list<T> split() {
      std::list<T> l;
      for (int i; next(i);) l.push_back(static_cast<T>(i));
      return l;
    }
  };
  auto operator[](_str key) const { return manip(_st->get(key)); }
public:
  static setting preferences();
  static setting preferences(_str name);
  static std::list<std::string> mailboxes();
  static setting mailbox(std::string const& id);
  static void mailboxclear(std::string const& id);
  static std::list<std::string> cache(std::string_view key);
  static void cache(std::string_view key, std::list<std::string> const& data);
  static void cacheclear();
  static char const* invalidchars();
  static bool edit();
public:
  std::string cipher(_str key);
  setting& cipher(_str key, std::string const& value);
  setting& erase(_str key) { _st->erase(key); return *this; }
};
