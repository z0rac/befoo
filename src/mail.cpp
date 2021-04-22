/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "win32.h"
#include <cassert>
#include <cctype>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** u8conv - convert multibyte text to UTF-8
 */
namespace {
  extern "C" {
    typedef HRESULT (WINAPI* ConvertINetMultiByteToUnicode)
      (LPDWORD, DWORD, LPCSTR, LPINT, LPWSTR, LPINT);
  }
#define FUNC(name) name(_dll(#name, {}))

  class u8conv {
    static win32::dll _dll;
    static ConvertINetMultiByteToUnicode _mb2u;
    UINT _codepage = 0;
    DWORD _mode = 0;
  public:
    u8conv& codepage(UINT codepage) noexcept;
    u8conv& reset() noexcept { _mode = 0; return *this; }
    explicit operator bool() const noexcept { return _codepage != 0; }
    std::string operator()(std::string const& text);
  };
  win32::dll u8conv::_dll("mlang.dll");
  ConvertINetMultiByteToUnicode
  u8conv::_mb2u = FUNC(ConvertINetMultiByteToUnicode);
#undef FUNC
}

u8conv&
u8conv::codepage(UINT codepage) noexcept
{
  if (codepage && codepage != _codepage) {
    _codepage = codepage, _mode = 0;
  }
  return *this;
}

std::string
u8conv::operator()(std::string const& text)
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
 * Functions of the class mail.
 */
bool
mail::header(std::string const& headers)
{
  bool read = false;
  decoder de(headers);
  while (de) {
    auto field = de.field({ "SUBJECT", "FROM", "DATE", "STATUS" });
    switch (field.first) {
    case 0: _subject = decoder(field.second).unstructured(); break;
    case 1: _from = decoder(field.second).address(); break;
    case 2: _date = decoder(field.second).date(); break;
    case 3: read = field.second.find('R') != field.second.npos; break;
    }
  }
  return read;
}

/*
 * Functions of the class mail::decoder
 */
std::pair<int, std::string>
mail::decoder::field(std::initializer_list<char const*> names)
{
  while (*this) {
    auto i = findf(":\015");
    if (i == _s.npos) {
      _next = _s.size();
      break;
    }
    if (_s[i] == ':') {
      auto name = uppercase(i++);
      auto n = 0;
      for (auto np : names) {
	if (np == name) break;
	++n;
      }
      if (n < names.size()) {
	_next = i;
	std::string s;
	do {
	  i = _s.find("\015\012", _next);
	  s.append(_s, _next, i - _next);
	  _next = i == _s.npos ? _s.size() : i + 2;
	} while (*this && (_s[_next] == ' ' || _s[_next] == '\t'));
	return { n, std::string(trim(s)) };
      }
    }
    do {
      i = _s.find("\015\012", i);
      i = i != _s.npos ? i + 2 : _s.size();
    } while (i < _s.size() && (_s[i] == ' ' || _s[i] == '\t'));
    _next = i;
  }
  return { -1, {} };
}

std::pair<std::string, std::string>
mail::decoder::address()
{
  std::string line, addr[4]; // $0 <$1> $2 ($3)
  int n = 0;
  for (auto first = true; *this;) {
    auto a0 = eword(findf("\"(,<>[\\"));
    line += a0;
    if (first) addr[n] += a0;
    auto t = token(true);
    if (t.empty()) break;
    if (t[0] == '(' || t[0] == '"') line += eword(t, false);
    else line += t;
    if (first) {
      switch (t[0]) {
      case ',': first = false; break;
      case '<': n = 1; break;
      case '>': n = 2; break;
      case '[': addr[n] += t; break;
      case '(': addr[3] = eword(t.substr(1, t.size() - 2), true); break;
      case '"': addr[n] += eword(t.substr(1, t.size() - 2), true); break;
      case '\\': addr[n] += t.substr(1); break;
      }
    }
  }
  for (auto& ad : addr) ad = trim(ad);
  n = (addr[1].empty() ? (addr[3].empty() ? 0 : 3) :
       addr[0].empty() ? (addr[3].empty() ? 1 : 3) : 0);
  return { addr[n], line };
}

time_t
mail::decoder::date()
{
  struct tm tms {};
  {
    auto day = token(), month = token();
    if (month == ",") day = token(), month = token();
    if (!digit(day, &tms.tm_mday)) return time_t(-1);
    constexpr char mn[][4] = { 
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int i = sizeof(mn) / sizeof(*mn);
    while (i-- && month != mn[i]) continue;
    if (i < 0) return time_t(-1);
    tms.tm_mon = i;
  }
  if (!digit(token(), &tms.tm_year)) return time_t(-1);
  tms.tm_year -= 1900;
  if (!digit(token(), &tms.tm_hour) ||
      token() != ":" ||
      !digit(token(), &tms.tm_min)) return time_t(-1);
  auto zone = token();
  if (zone == ":") {
    if (!digit(token(), &tms.tm_sec)) return time_t(-1);
    zone = token();
  }

  auto gmt = mktime(&tms);
  if (gmt == time_t(-1)) return time_t(-1);
  auto gm = gmtime(&gmt);
  if (!gm) return time_t(-1);
  gmt += tms.tm_sec - gm->tm_sec;
  gmt += (tms.tm_min - gm->tm_min) * 60;
  gmt += (tms.tm_hour - gm->tm_hour) * 3600;
  if (tms.tm_mday != gm->tm_mday) gmt += 86400;

  int delta = 0;
  if (!zone.empty()) {
    if (zone[0] == '+' || zone[0] == '-') {
      if (!digit(zone.substr(1), &delta)) return time_t(-1);
      delta *= (zone[0] != '-') * 2 - 1;
    } else if (zone.size() == 3 && zone[2] == 'T' &&
	       (zone[1] == 'S' || zone[1] == 'D')) {
      constexpr char z[] = "ECMP";
      for (auto i = 0; !delta && z[i]; ++i) {
	if (zone[0] == z[i]) delta = (i + (zone[1] == 'S') + 4) * -100;
      }
    }
  }
  return gmt - (delta / 100 * 60 + delta % 100) * 60;
}

std::string_view
mail::decoder::trim(std::string_view text)
{
  constexpr char ws[] = "\t ";
  if (auto i = text.find_first_not_of(ws); i != text.npos) {
    return text.substr(i, text.find_last_not_of(ws) - i + 1);
  }
  return {};
}

std::string
mail::decoder::eword(size_type to)
{
  if (auto i = _next; i < to) {
    _next = min(to, _s.size());
    return eword(std::string_view(_s).substr(i, _next - i));
  }
  return {};
}

std::string
mail::decoder::eword(std::string_view text, bool unescape)
{
  std::string result;
  while (!text.empty()) {
    auto i = text.find('\\');
    result += eword(text.substr(0, i));
    if (i == text.npos) break;
    auto e = std::min<size_t>(i + 2, text.size());
    i += unescape;
    result += text.substr(i, e - i);
    text = text.substr(e);
  }
  return result;
}

std::string
mail::decoder::eword(std::string_view text)
{
  std::string result;
  for (u8conv conv;;) {
    size_type prefix = 0, eword = 0;
    std::string_view q[3];
    for (;; prefix += 2) {
      auto i = text.find("=?", prefix);
      if (i == text.npos) break;
      prefix = i, i += 2;
      int n = 0;
      for (; n < 3; ++n) {
	constexpr char const* delim[] { "\t ()<>@,;:\"/[]?.=", delim[0], "\t ?" };
	auto s = i;
	i = text.find_first_of(delim[n], i);
	if (i == text.npos || text[i] != '?') break;
	q[n] = text.substr(s, i++ - s);
      }
      if (n < 3 || i == text.size() || text[i++] != '=') continue;
      eword = i - prefix;
      break;
    }
    if (!eword) break;
    if (auto t = text.substr(0, prefix); t.find_first_not_of("\t ") != text.npos) {
      result += t, conv.reset();
    }
    decltype(&decodeB) decode = {};
    if (q[1].size() == 1) {
      switch (std::toupper(q[1][0])) {
      case 'B': decode = decodeB; break;
      case 'Q': decode = decodeQ; break;
      }
    }
    if (decode) {
      try {
	extern unsigned codepage(std::string_view);
	result += conv.codepage(codepage(q[0]))(decode(q[2]));
      } catch (...) {
	decode = {};
      }
    }
    if (!decode) result += text.substr(prefix, eword), conv.reset();
    text = text.substr(prefix + eword);
  }
  result += text;
  return result;
}

std::string
mail::decoder::decodeB(std::string_view text)
{
  if (text.size() & 3) throw -1;
  std::string decode;
  decode.reserve(text.size() / 4 * 3);
  for (; !text.empty(); text = text.substr(4)) {
    unsigned v = 0;
    int i = 0;
    for (; i < 4; ++i) {
      constexpr char b64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      if (auto p = strchr(b64, text[i]); p) v = (v << 6) | unsigned(p - b64);
      else break;
    }
    if (i < 4) {
      if (i < 2 || text.size() > 4) throw -1;
      for (auto t = i; t < 4; ++t) {
	if (text[t] != '=') throw -1;
	v <<= 6;
      }
    }
    char const b[] { char(v >> 16), char(v >> 8), char(v) };
    decode.append(b, i - 1);
  };
  return decode;
}

std::string
mail::decoder::decodeQ(std::string_view text)
{
  std::string decode;
  while (!text.empty()) {
    auto i = text.find_first_of("_=");
    if (i) decode += text.substr(0, i);
    if (i == text.npos) break;
    auto c = ' ';
    if (text[i++] == '=') {
      if (i >= text.size() - 1) throw -1;
      char const s[] { text[i], text[i + 1], 0 };
      char* e;
      c = char(strtoul(s, &e, 16));
      if (e != s + 2) throw -1;
      i += 2;
    }
    decode += c;
    text = text.substr(i);
  }
  return decode;
}

std::string_view
mail::decoder::token(bool comment)
{
  while (*this) {
    auto i = _s.find_first_not_of("\t ", _next);
    if (i == _s.npos) {
      _next = _s.size();
      break;
    }
    auto s = i;
    i = findf("\t \"(),.:;<>@[\\]", i);
    if (i == s || (i != _s.npos && _s[i] == '.')) {
      switch (_s[i]) {
      case '"': // quoted-text
	i = findq("\"", i + 1);
	break;
      case '(': // comment
	for (auto nest = 1; nest;) {
	  i = findq("()", i + 1);
	  if (i == _s.npos) break;
	  nest += (_s[i] == '(') * 2 - 1;
	}
	break;
      case '.': // dot-atom
	i = findf("\t \"(),:;<>@[\\]", i + 1);
	i -= i != _s.npos;
	break;
      case '[': // domain
	i = findq("]", i + 1);
	break;
      case '\\': // escape
	++i;
	break;
      }
      i += i < _s.size();
    }
    _next = i != _s.npos ? i : _s.size();
    if (comment || _s[s] != '(') return std::string_view(_s).substr(s, _next - s);
  }
  return {};
}

/*
 * Functions of the class tokenizer
 */
bool
tokenizer::digit(std::string_view s, int* value) noexcept
{
  if (s.empty()) return false;
  auto v = 0;
  auto p = s.cbegin(), e = s.cend();
  while (p != e && *p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  if (value) *value = v;
  return p == e;
}

std::string
tokenizer::uppercase(std::string_view s)
{
  std::string u;
  u.reserve(s.size());
  for (auto c : s) u.push_back(static_cast<char>(std::toupper(c)));
  return u;
}

tokenizer::size_type
tokenizer::findq(char const* s, size_type pos) const
{
  std::string delim = s;
  delim += '\\';
  for (auto i = pos; i < _s.size(); i += 2) {
    i = _s.find_first_of(delim, i);
    if (i == _s.npos || _s[i] != '\\') return i;
  }
  return _s.npos;
}
