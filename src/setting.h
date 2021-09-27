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
    _str(char const* s) : c_str(s) {}
    _str(std::string const& s) : c_str(s.c_str()) {}
    operator char const*() const { return c_str; }
  };
  class _profile;
  class _registory;
  class storage {
  public:
    virtual ~storage() {}
    virtual std::string get(char const* key) const = 0;
    virtual void put(char const* key, char const* value) = 0;
    virtual void erase(char const* key) = 0;
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
    virtual std::unique_ptr<setting::storage> storage(std::string const& name) const = 0;
    virtual std::list<std::string> storages() const = 0;
    virtual void erase(std::string const& name) = 0;
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
  class tuple {
    std::string _s;
    char _sep;
    tuple& add(std::string const& s);
    static std::string digit(long i);
  public:
    tuple(std::string const& v, char sep = ',') : _s(v), _sep(sep) {}
    template<class T> tuple(T v, char sep = ',') : _s(digit(v)), _sep(sep) {}
    auto& operator()(std::string const& v) { return add(v); }
    template<class T> auto& operator()(T v) { return add(digit(v)); }
    auto& row() const noexcept { return _s; }
    operator std::string const&() const noexcept { return _s; }
  };
  auto& operator()(_str key, std::string const& value)
  { return _st->put(key, value.c_str()), *this; }
  auto& operator()(_str key, char const* value) { return _st->put(key, value), *this; }
  auto& operator()(_str key, long value) { return operator()(key, tuple(value)); }

  // manip - use for separated input parameters.
  // Examples:
  //   s = setting::preferences()["title"];
  //   setting::preferences()["windowpos"](x)(y)(w)(h);
  class manip {
    std::string _s;
    std::string::size_type _next = 0;
    char _sep = ',';
    auto avail() const { return _next < _s.size(); }
    std::string next();
    bool next(int& v);
  public:
    manip(std::string const& s) : _s(s) {}
    manip& operator()() { return next(), *this; }
    manip& operator()(std::string& v);
    template<class T> auto& operator()(T& v)
    { int i = int(v); next(i), v = T(i); return *this; }
    template<class T, typename U> auto& operator()(T& v, U& e)
    { int i = int(v); e = U(next(i)), v = T(i); return *this; }
    auto& operator()(int& v) { return next(v), *this; }
    template<class T> auto& operator()(int& v, T& e) { return e = T(next(v)), *this; }
    auto& row() const noexcept { return _s; }
    operator std::string const&() const { return _s; }
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
