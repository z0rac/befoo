/*
 * Copyright (C) 2010-2012 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "win32.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

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
namespace {
  string::size_type
  find(const string& s, string::value_type c, string::size_type i = 0)
  {
    while (i < s.size()) {
      if (s[i] == c) return i;
      i += (i + 1 < s.size() && IsDBCSLeadByte(BYTE(s[i]))) + 1;
    }
    return string::npos;
  }
}

uri::uri(const string& uri)
{
  struct lambda {
    static string decode(const string& s) {
      string result;
      string::size_type i = 0;
      while (i < s.size()) {
	string::size_type n = find(s, '%', i);
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
  };

  string::size_type i = find(uri, '#');
  if (i != string::npos) _part[fragment] = lambda::decode(uri.substr(i + 1));
  
  string t(uri, 0, min(find(uri, '?'), i));
  i = find(t, ':');
  for (; i != string::npos; i = find(t, ':', i)) {
    if (++i == t.size() || t[i] != '/') continue;
    if (++i == t.size() || t[i] != '/') continue;
    _part[scheme] = lambda::decode(t.substr(0, i - 2));
    t.erase(0, i + 1);
    break;
  }
  i = find(t, '/');
  if (i != string::npos) {
    _part[path] = lambda::decode(t.substr(i + 1));
    t.erase(i);
  }
  i = find(t, '@');
  if (i != string::npos) {
    _part[user] = lambda::decode(t.substr(0, min(find(t, ';'), i)));
    t.erase(0, i + 1);
  }
  i = find(t, ':');
  for (; i != string::npos; i = find(t, ':', i)) {
    string::size_type n = i;
    while (++i < t.size() && strchr("0123456789", t[i])) continue;
    if (i < t.size()) continue;
    _part[port] = t.substr(n + 1);
    t.erase(n);
  }
  _part[host] = lambda::decode(t);
}

uri::operator string() const
{
  struct lambda {
    static string encode(const string& s, const char* ex = "") {
      string result;
      string::size_type i = 0;
      while (i < s.size()) {
	const char* const t = s.c_str();
	const char* p = t + i;
	while (BYTE(*p) > 32) {
	  string::size_type n = IsDBCSLeadByte(BYTE(*p)) ?
	    (p[1] != 0) * 2 : !strchr(":/?#[]@%", *p) || strchr(ex, *p);
	  p += n;
	  if (n == 0) break;
	}
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
  };

  string uri = _part[scheme] + "://";
  if (!_part[user].empty()) uri += lambda::encode(_part[user], ":") + '@';
  if (!_part[host].empty()) {
    const string& s = _part[host];
    uri += *s.begin() == '[' && *s.rbegin() == ']' ?
      '[' + lambda::encode(s.substr(1, s.size() - 2), ":") + ']' : lambda::encode(s);
  }
  if (!_part[port].empty()) uri += ':' + _part[port];
  uri += '/' + lambda::encode(_part[path], ":@/");
  if (!_part[fragment].empty()) uri += '#' + lambda::encode(_part[fragment], ":@/?");
  return uri;
}

namespace {
  string idn(const string& host)
  {
    return host;
  }

  string utf7m(const string& path)
  {
    string::size_type i = 0;
    while (i < path.size() && path[i] >= ' ' && path[i] <= '~' && path[i] != '&') ++i;
    if (i == path.size()) return path;
    string encoded = path.substr(0, i);
    win32::wstr ws = path.substr(i);
    LPCWSTR p = ws;
    while (*p) {
      encoded += '&';
      if (*p != L'&') {
	static const char b64[] =
	  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
	unsigned wc = 0;
	int n = 0;
	while (*p && (*p < L' ' || *p > L'~')) {
	  wc = (wc << 16) + *p++, n += 16;
	  for (; n >= 6; n -= 6) encoded += b64[(wc >> (n - 6)) & 63];
	}
	if (n) encoded += b64[(wc << (6 - n)) & 63];
      } else ++p;
      encoded += '-';
      for (; *p && *p >= L' ' && *p <= L'~' && *p != L'&'; ++p) {
	encoded += static_cast<string::value_type>(*p);
      }
    }
    return encoded;
  }
}

string
uri::encoded(int i) const
{
  switch (i) {
  case host: return idn(_part[host]);
  case path: return utf7m(_part[path]);
  default: return _part[i];
  }
}
