#ifndef H_SETTING /* -*- mode: c++ -*- */
/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
    virtual ~_repository() {}
    virtual string get(const char* key) const = 0;
    virtual void put(const char* key, const char* value) = 0;
  };
  auto_ptr<_repository> _rep;
  setting(_repository* rep) : _rep(rep) {}
public:
  typedef _repository repository;
  setting(const setting& s) : _rep(const_cast<setting&>(s)._rep) {}
  setting operator=(const setting& s)
  { _rep = const_cast<setting&>(s)._rep; return *this; }
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
    operator const string&() const { return _s; }
  };
  setting& operator()(_str key, const string& value)
  { _rep->put(key, value.c_str()); return *this; }
  setting& operator()(_str key, const char* value)
  { _rep->put(key, value); return *this; }

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
    bool next(long& v);
  public:
    manip(const string& s);
    manip& operator()(string& v);
    template<typename _Ty> manip& operator()(_Ty& v)
    { long i; next(i) && (v = static_cast<_Ty>(i)); return *this; }
    operator const string&() const { return _s; }
    manip& sep(char sep) { _sep = sep; return *this; }
    list<string> split();

    template<typename _Ty>
    list<_Ty> split()
    {
      list<_Ty> l;
      long i;
      while (next(i)) l.push_back(static_cast<_Ty>(i));
      return l;
    }
  };
  manip operator[](_str key) const { return manip(_rep->get(key)); }
public:
  static setting preferences();
  static list<string> mailboxes();
  static setting mailbox(const string& id);
  static bool edit();
public:
  string cipher(_str key);
  setting& cipher(_str key, const string& value);
};

#endif
