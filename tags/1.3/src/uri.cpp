/*
 * Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of the class uri
 */
string
uri::_encode(const string& s, const char* ex)
{
  string result;
  string::size_type i = 0;
  while (i < s.size()) {
    const char* const t = s.c_str();
    const char* p = t + i;
    while (*p > 32 && *p < 127 && (!strchr(":/?#[]@%", *p) || strchr(ex, *p))) ++p;
    if (!*p) break;
    string::size_type n = p - t;
    result.append(s, i, n - i);
    char hex[4] = { '%' };
    _ltoa(s[n] & 255, hex + 1, 16);
    result.append(hex);
    i = n + 1;
  }
  if (i < s.size()) result.append(s.c_str() + i);
  return result;
}

string
uri::_decode(const string& s)
{
  string result;
  string::size_type i = 0;
  while (i < s.size()) {
    string::size_type n = s.find_first_of('%', i);
    if (n == string::npos || s.size() - n < 3) break;
    result.append(s, i, n - i);
    char hex[3] = { s[n + 1], s[n + 2] };
    char* e;
    char c = char(strtoul(hex, &e, 16));
    i = n + (*e ? (c = '%', 1) : 3);
    result.push_back(c);
  }
  if (i < s.size()) result.append(s.c_str() + i);
  return result;
}

uri::uri(const string& uri)
{
  string::size_type i = uri.find_first_of('#');
  if (i != string::npos) _part[fragment] = _decode(uri.substr(i + 1));

  string t(uri, 0, min(uri.find_first_of('?'), i));
  i = t.find("://");
  if (i != string::npos) {
    _part[scheme] = _decode(t.substr(0, i));
    t.erase(0, i + 3);
  }
  i = t.find_first_of('/');
  if (i != string::npos) {
    _part[path] = _decode(t.substr(i + 1));
    t.erase(i);
  }
  i = t.find_first_of('@');
  if (i != string::npos) {
    _part[user] = _decode(t.substr(0, min(t.find_first_of(';'), i)));
    t.erase(0, i + 1);
  }
  i = t.find_last_not_of("0123456789");
  if (i != string::npos && t[i] == ':') {
    _part[port] = t.substr(i + 1);
    t.erase(i);
  }
  _part[host] = _decode(t);
}

uri::operator string() const
{
  string uri = _part[scheme] + "://";
  if (!_part[user].empty()) uri += _encode(_part[user], ":") + '@';
  if (!_part[host].empty()) {
    const string& s = _part[host];
    uri += *s.begin() == '[' && *s.rbegin() == ']' ?
      '[' + _encode(s.substr(1, s.size() - 2), ":") + ']' : _encode(s);
  }
  if (!_part[port].empty()) uri += ':' + _part[port];
  uri += '/' + _encode(_part[path], ":@/");
  if (!_part[fragment].empty()) uri += '#' + _encode(_part[fragment], ":@/?");
  return uri;
}
