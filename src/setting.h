#ifndef H_SETTING /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#define H_SETTING

#include <list>
#include <memory>
#include <string>

using namespace std;

class setting {
  class _repository {
  public:
    class _storage {
    public:
      virtual ~_storage() {}
      virtual string get(const char* key) const = 0;
      virtual void put(const char* key, const char* value) = 0;
      virtual void erase(const char* key) = 0;
      virtual list<string> keys() const = 0;
    };
  public:
    _repository();
    virtual ~_repository() {}
    virtual _storage* storage(const string& name) const = 0;
    virtual list<string> storages() const = 0;
    virtual void erase(const string& name) = 0;
    virtual const char* invalidchars() const = 0;
  };
  auto_ptr<_repository::_storage> _st;
  setting(_repository::_storage* st) : _st(st) {}
public:
  typedef _repository repository;
  typedef _repository::_storage storage;
  setting(const setting& s) : _st(const_cast<setting&>(s)._st) {}
  const setting& operator=(const setting& s)
  { _st = const_cast<setting&>(s)._st; return *this; }
public:
  struct _str {
    const char* c_str;
    _str(const char* s) : c_str(s) {}
    _str(const string& s) : c_str(s.c_str()) {}
    operator const char*() const { return c_str; }
  };
public:
  // tuple - use for separated output parameters.
  // Example:
  //   setting::preferences()("windowpos", setting::tuple(x)(y)(w)(h));
  class tuple {
    string _s;
    char _sep;
    tuple& add(const string& s);
    static string digit(long i);
  public:
    tuple(const string& v, char sep = ',') : _s(v), _sep(sep) {}
    template<typename _Ty>
    tuple(_Ty v, char sep = ',') : _s(digit(v)), _sep(sep) {}
    tuple& operator()(const string& v) { return add(v); }
    template<typename _Ty>
    tuple& operator()(_Ty v) { return add(digit(v)); }
    const string& row() const { return _s; }
    operator const string&() const { return _s; }
  };
  setting& operator()(_str key, const string& value)
  { _st->put(key, value.c_str()); return *this; }
  setting& operator()(_str key, const char* value)
  { _st->put(key, value); return *this; }
  setting& operator()(_str key, long value)
  { return operator()(key, tuple(value)); }

  // manip - use for separated input parameters.
  // Examples:
  //   s = setting::preferences()["title"];
  //   setting::preferences()["windowpos"](x)(y)(w)(h);
  class manip {
    string _s;
    string::size_type _next;
    char _sep;
    bool avail() const { return _next < _s.size(); }
    string next();
    bool next(int& v);
  public:
    manip(const string& s);
    manip& operator()() { next(); return *this; }
    manip& operator()(string& v);
    template<typename _Ty> manip& operator()(_Ty& v)
    { int i = int(v); next(i), v = _Ty(i); return *this; }
    template<typename _Ty, typename _Tz> manip& operator()(_Ty& v, _Tz& e)
    { int i = int(v); e = _Tz(next(i)), v = _Ty(i); return *this; }
    manip& operator()(int& v) { next(v); return *this; }
    template<typename _Tz> manip& operator()(int& v, _Tz& e)
    { e = _Tz(next(v)); return *this; }
    const string& row() const { return _s; }
    operator const string&() const { return _s; }
    manip& sep(char sep) { _sep = sep; return *this; }
    list<string> split();

    template<typename _Ty>
    list<_Ty> split()
    {
      list<_Ty> l;
      for (int i; next(i);) l.push_back(static_cast<_Ty>(i));
      return l;
    }
  };
  manip operator[](_str key) const { return manip(_st->get(key)); }
public:
  static setting preferences();
  static setting preferences(_str name);
  static list<string> mailboxes();
  static setting mailbox(const string& id);
  static void mailboxclear(const string& id);
  static list<string> cache(const string& key);
  static void cache(const string& key, const list<string>& data);
  static void cacheclear();
  static const char* invalidchars();
public:
  string cipher(_str key);
  setting& cipher(_str key, const string& value);
  setting& erase(_str key) { _st->erase(key); return *this; }
};

class registory : public setting::repository {
  void* _key;
public:
  registory(const char* key);
  ~registory();
  setting::storage* storage(const string& name) const;
  list<string> storages() const;
  void erase(const string& name);
  const char* invalidchars() const { return "\\"; }
};

class profile : public setting::repository {
  const char* _path;
public:
  profile(const char* path) : _path(path) {}
  ~profile();
  setting::storage* storage(const string& name) const;
  list<string> storages() const;
  void erase(const string& name);
  const char* invalidchars() const { return "]"; }
};

#endif
