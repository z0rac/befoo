/*
 * Copyright (C) 2010-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"

/*
 * Functions of the class uri
 */
namespace {
  size_t find(std::string_view s, char c, size_t i = 0)
  {
    while (i < s.size()) {
      if (s[i] == c) return i;
      i += (i + 1 < s.size() && IsDBCSLeadByte(BYTE(s[i]))) + 1;
    }
    return s.npos;
  }

  std::string decode(std::string_view s)
  {
    std::string result;
    while (!s.empty()) {
      auto i = find(s, '%');
      if (i == s.npos || s.size() - i < 3) break;
      result += s.substr(0, i);
      char hex[3] { s[i + 1], s[i + 2] };
      char* e;
      char c = char(strtoul(hex, &e, 16));
      s = s.substr(i + (*e ? (c = '%', 1) : 3));
      result.push_back(c);
    }
    if (!s.empty()) result += s;
    return result;
  }

  std::string encode(std::string_view s, std::string_view chs)
  {
    std::string result;
    while (!s.empty()) {
      size_t i = 0;
      while (i < s.size() && BYTE(s[i]) > 32) {
	auto c = s[i];
	auto n = IsDBCSLeadByte(BYTE(c)) ?
	  (i + 1 < s.size()) * 2 : chs.find(c) == chs.npos;
	i += n;
	if (n == 0) break;
      }
      if (i == s.size()) break;
      result.append(s, 0, i);
      constexpr auto h4 = [](auto v) { return "0123456789ABCDEF"[v & 15]; };
      char hex[4] { '%', h4(s[i] >> 4), h4(s[i]) };
      result.append(hex);
      s = s.substr(i + 1);
    }
    if (!s.empty()) result += s;
    return result;
  }
}

uri::uri(std::string_view uri)
{
  if (auto i = find(uri, '#'); i != uri.npos) {
    _part[fragment] = decode(uri.substr(i + 1));
    uri = uri.substr(0, i);
  }
  uri = uri.substr(0, find(uri, '?'));
  for (auto i = find(uri, ':'); i != uri.npos; i = find(uri, ':', i)) {
    if (++i == uri.size() || uri[i] != '/') continue;
    if (++i == uri.size() || uri[i] != '/') continue;
    _part[scheme] = decode(uri.substr(0, i - 2));
    uri = uri.substr(i + 1);
    break;
  }
  if (auto i = find(uri, '/'); i != uri.npos) {
    _part[path] = decode(uri.substr(i + 1));
    uri = uri.substr(0, i);
  }
  if (auto i = find(uri, '@'); i != uri.npos) {
    _part[user] = decode(uri.substr(0, min(find(uri, ';'), i)));
    uri = uri.substr(i + 1);
  }
  for (auto i = find(uri, ':'); i != uri.npos; i = find(uri, ':', i)) {
    auto n = i;
    if (i = uri.find_first_not_of("0123456789", i + 1); i == uri.npos) {
      _part[port] = uri.substr(n + 1);
      uri = uri.substr(0, n);
    }
  }
  _part[host] = decode(uri);
}

uri::operator std::string() const
{
  auto uri = encode(_part[scheme], "#%") + "://";
  if (!_part[host].empty()) {
    if (!_part[user].empty()) uri += encode(_part[user], "@/#%") + '@';
    auto& h = _part[host];
    uri += encode(h, h[0] == '[' && *h.rbegin() == ']' ? "/#%" : ":/#%");
    if (!_part[port].empty()) uri += ':' + _part[port];
  }
  uri += '/' + encode(_part[path], "#%");
  if (!_part[fragment].empty()) uri += '#' + encode(_part[fragment], "%");
  return uri;
}
