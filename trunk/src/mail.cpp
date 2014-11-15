/*
 * Copyright (C) 2009-2014 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#if USE_ICONV
/** u8conv - iconv wrapper
 */
namespace {
  extern "C" {
    typedef void* iconv_t;
    typedef iconv_t (*libiconv_open)(const char*, const char*);
    typedef size_t (*libiconv)(iconv_t, const char**, size_t*, char**, size_t*);
    typedef int (*libiconv_close)(iconv_t);
  }
#define FUNC(name) name(_dll(#name))

  class u8conv {
    static win32::dll _dll;
    iconv_t _cd;
    string _charset;
    libiconv_open _open;
    libiconv_close _close;
    libiconv _iconv;
  public:
    u8conv() : _cd(iconv_t(-1)), _open(NULL) {}
    ~u8conv() { if (*this) FUNC(libiconv_close)(_cd); }
    u8conv& charset(const string& charset);
    u8conv& reset();
    operator bool() const { return _cd != iconv_t(-1); }
    string operator()(const string& text);
  };
  win32::dll u8conv::_dll("iconv.dll");
}

u8conv&
u8conv::charset(const string& charset)
{
  if (_dll) {
    if (!_open) {
      _iconv = FUNC(libiconv);
      _close = FUNC(libiconv_close);
      _open = FUNC(libiconv_open);
    }
    if (!charset.empty() && charset != _charset) {
      if (_cd != iconv_t(-1)) _close(_cd), _cd = iconv_t(-1);
      _charset = charset;
      _cd = _open("UTF-8", charset.c_str());
    }
  }
  return *this;
}

u8conv&
u8conv::reset()
{
  if (_cd != iconv_t(-1)) _iconv(_cd, NULL, NULL, NULL, NULL);
  return *this;
}

string
u8conv::operator()(const string& text)
{
  if (!*this) throw text;
  string result;
  const char* in = text.c_str();
  size_t inlen = text.size();
  size_t ret;
  do {
    char buf[128];
    char* out = buf;
    size_t outlen = sizeof(buf);
    ret = _iconv(_cd, &in, &inlen, &out, &outlen);
    if (outlen == sizeof(buf)) break;
    result.append(buf, sizeof(buf) - outlen);
  } while (ret == size_t(-1));
  return result;
}

#undef FUNC
#else // !USE_ICONV
/** u8conv - convert multibyte text to UTF-8
 */
namespace {
  extern "C" {
    typedef HRESULT (WINAPI* ConvertINetMultiByteToUnicode)
      (LPDWORD, DWORD, LPCSTR, LPINT, LPWSTR, LPINT);
  }
#define FUNC(name) name(_dll(#name, NULL))

  class u8conv {
    static win32::dll _dll;
    static ConvertINetMultiByteToUnicode _mb2u;
    string _charset;
    UINT _codepage;
    DWORD _mode;
  public:
    u8conv() : _codepage(0) {}
    u8conv& charset(const string& charset);
    u8conv& reset() { _mode = 0; return *this; }
    operator bool() const { return _codepage != 0; }
    string operator()(const string& text);
  };
  win32::dll u8conv::_dll("mlang.dll");
  ConvertINetMultiByteToUnicode
  u8conv::_mb2u = FUNC(ConvertINetMultiByteToUnicode);
#undef FUNC
}

extern unsigned codepage(const string&);

u8conv&
u8conv::charset(const string& charset)
{
  if (!charset.empty() && charset != _charset) {
    _charset = charset;
    _codepage = codepage(charset);
    _mode = 0;
  }
  return *this;
}

string
u8conv::operator()(const string& text)
{
  if (!*this) throw text;
  if (!_mb2u) {
    win32::wstr ws(text, _codepage);
    if (!ws) throw text;
    return ws.mbstr(CP_UTF8);
  }
  int n = 0;
  if (_mb2u(&_mode, _codepage, text.c_str(), NULL, NULL, &n) != S_OK) throw text;
  win32::textbuf<WCHAR> buf(n + 1);
  _mb2u(&_mode, _codepage, text.c_str(), NULL, buf.data, &n);
  buf.data[n] = 0;
  return win32::wstr::mbstr(buf.data, CP_UTF8);
}
#endif // !USE_ICONV

/*
 * Functions of the class mail.
 */
bool
mail::header(const string& headers)
{
  bool read = false;
  decoder de(headers);
  while (de) {
    switch (de.field("SUBJECT\0FROM\0DATE\0STATUS\0")) {
    case 0: _subject = decoder(de.field()).unstructured(); break;
    case 1: _from = decoder(de.field()).address(); break;
    case 2: _date = decoder(de.field()).date(); break;
    case 3: read = de.field().find_first_of('R') != string::npos; break;
    }
  }
  return read;
}

/*
 * Functions of the class mail::decoder
 */
int
mail::decoder::field(const char* names)
{
  _field.clear();
  while (*this) {
    string::size_type i = findf(":\015");
    if (i == string::npos) {
      _next = _s.size();
      break;
    }
    if (_s[i] == ':') {
      string name = uppercase(i++);
      int n = 0;
      const char* np = names;
      for (; *np && name != np; np += strlen(np) + 1) ++n;
      if (*np) {
	_next = i;
	string s;
	do {
	  i = _s.find("\015\012", _next);
	  s.append(_s, _next, i - _next);
	  _next = i == string::npos ? _s.size() : i + 2;
	} while (*this && (_s[_next] == ' ' || _s[_next] == '\t'));
	_field = trim(s);
	return n;
      }
    }
    do {
      i = _s.find("\015\012", i);
      i = i != string::npos ? i + 2 : _s.size();
    } while (i < _s.size() && (_s[i] == ' ' || _s[i] == '\t'));
    _next = i;
  }
  return -1;
}

pair<string, string>
mail::decoder::address()
{
  string line;
  bool first = true;
  string addr[4]; // $0 <$1> $2 ($3)
  int n = 0;
  while (*this) {
    string t = eword(findf("\"(,<>[\\"));
    line += t;
    if (first) addr[n] += t;
    t = token(true);
    if (t.empty()) break;
    line += t[0] == '(' || t[0] == '"' ? eword(t, false) : t;
    if (first) {
      switch (t[0]) {
      case ',': first = false; break;
      case '<': n = 1; break;
      case '>': n = 2; break;
      case '[': addr[n] += t; break;
      case '(': addr[3] = eword(t.assign(t, 1, t.size() - 2), true); break;
      case '"': addr[n] += eword(t.assign(t, 1, t.size() - 2), true); break;
      case '\\': addr[n] += t.substr(1); break;
      }
    }
  }
  for (int i = 0; i < 4; ++i) addr[i] = trim(addr[i]);
  n = (addr[1].empty() ? (addr[3].empty() ? 0 : 3) :
       addr[0].empty() ? (addr[3].empty() ? 1 : 3) : 0);
  return pair<string, string>(addr[n], line);
}

time_t
mail::decoder::date()
{
  struct tm tms = { 0 };
  {
    string day = token(), month = token();
    if (month == ",") day = token(), month = token();
    if (!digit(day, tms.tm_mday)) return time_t(-1);
    static const char mn[][4] = { 
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    int i = sizeof(mn) / sizeof(*mn);
    while (i-- && month != mn[i]) continue;
    if (i < 0) return time_t(-1);
    tms.tm_mon = i;
  }
  if (!digit(token(), tms.tm_year)) return time_t(-1);
  tms.tm_year -= 1900;
  if (!digit(token(), tms.tm_hour) ||
      token() != ":" ||
      !digit(token(), tms.tm_min)) return time_t(-1);
  string zone = token();
  if (zone == ":") {
    if (!digit(token(), tms.tm_sec)) return time_t(-1);
    zone = token();
  }

  time_t gmt = mktime(&tms);
  if (gmt == time_t(-1)) return time_t(-1);
  struct tm* gm = gmtime(&gmt);
  if (!gm) return time_t(-1);
  gmt += tms.tm_sec - gm->tm_sec;
  gmt += (tms.tm_min - gm->tm_min) * 60;
  gmt += (tms.tm_hour - gm->tm_hour) * 3600;
  if (tms.tm_mday != gm->tm_mday) gmt += 86400;

  int delta = 0;
  if (!zone.empty()) {
    if (zone[0] == '+' || zone[0] == '-') {
      if (!digit(zone.substr(1), delta)) return time_t(-1);
      if (zone[0] == '-') delta = -delta;
    } else if (zone.size() == 3 && zone[2] == 'T' &&
	       (zone[1] == 'S' || zone[1] == 'D')) {
      static const char z[] = "ECMP";
      for (int i = 0; !delta && z[i]; ++i) {
	if (zone[0] == z[i]) delta = (i + int(zone[1] == 'S') + 4) * -100;
      }
    }
  }
  return gmt - (delta / 100 * 60 + delta % 100) * 60;
}

string
mail::decoder::trim(const string& text)
{
  static const char ws[] = " \t";
  string::size_type i = text.find_first_not_of(ws);
  return i == string::npos ? string() :
    text.substr(i, text.find_last_not_of(ws) - i + 1);
}

string
mail::decoder::eword(string::size_type to)
{
  string::size_type i = _next;
  return i < to ? eword(_s, i, _next = min(to, _s.size())) : string();
}

string
mail::decoder::eword(const string& text, bool unescape)
{
  string result;
  for (string::size_type i = 0; i < text.size();) {
    string::size_type n = text.find_first_of('\\', i);
    result += eword(text, i, n);
    if (n == string::npos) break;
    i = n < text.size() - 1 ? n + 2 : text.size();
    if (unescape) ++n;
    result.append(text, n, i - n);
  }
  return result;
}

string
mail::decoder::eword(const string& text,
		     string::size_type pos, string::size_type end)
{
  u8conv conv;
  string result;
  if (end > text.size()) end = text.size();
  for (string::size_type i = pos; i < end;) {
    i = text.find("=?", i);
    if (i >= end) break;
    i += 2;
    string::size_type q[3];
    {
      string::size_type p = i;
      int n = 0;
      for (; n < 3; ++n) {
	q[n] = p;
	p = text.find_first_of("? \t", p);
	if (p >= end || text[p++] != '?') break;
      }
      if (n < 3 || p == end || text[p++] != '=') continue;
      i = p;
    }
    int c = toupper(text[q[1]]);
    if (q[2] - q[1] != 2 || (c != 'B' && c != 'Q')) continue;
    if (text.find_first_not_of(" \t", pos) < q[0] - 2) {
      result.append(text, pos, q[0] - pos - 2), pos = q[0] - 2;
      conv.reset();
    }
    try {
      string s(text, q[2], i - q[2] - 2);
      s = (c == 'B' ? decodeB : decodeQ)(s);
      string cs = uppercase(text.substr(q[0], q[1] - q[0] - 1));
      result += cs == "UTF-8" ? (conv.reset(), s) : conv.charset(cs)(s);
      pos = i;
    } catch (...) {}
  }
  if (pos < end) result.append(text, pos, end - pos);
  return result;
}

string
mail::decoder::decodeB(const string& text)
{
  if (text.size() & 3) throw -1;
  string result;
  result.reserve((text.size() >> 2) * 3);
  for (const char* p = text.c_str(); *p; p += 4) {
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; ++i) {
      static const char b64[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
      const char* pos = strchr(b64, p[i]);
      if (!pos) break;
      v = (v << 6) | unsigned(pos - b64);
    }
    if (i < 4) {
      if (i < 2 || p[4]) throw -1;
      for (int t = i; t < 4; ++t) {
	if (p[t] != '=') throw -1;
	v <<= 6;
      }
    }
    char b[] = { char(v >> 16), char(v >> 8), char(v) };
    result.append(b, i - 1);
  };
  return result;
}

string
mail::decoder::decodeQ(const string& text)
{
  string result;
  for (string::size_type i = 0; i < text.size();) {
    string::size_type t = i;
    i = text.find_first_of("_=", i);
    if (i != t) result.append(text, t, i - t);
    if (i == string::npos) break;
    char c = char(0x20);
    if (text[i++] == '=') {
      if (i >= text.size() - 1) throw -1;
      char s[] = { text[i], text[i + 1], 0 };
      char* e;
      c = char(strtoul(s, &e, 16));
      if (e != s + 2) throw -1;
      i += 2;
    }
    result += c;
  }
  return result;
}

string
mail::decoder::token(bool comment)
{
  while (*this) {
    string::size_type i = _s.find_first_not_of(" \t", _next);
    if (i == string::npos) {
      _next = _s.size();
      break;
    }
    string::size_type n = findf(" \t\"(),.:;<>@[\\]", i);
    if (n == i || (n != string::npos && _s[n] == '.')) {
      switch (_s[n]) {
      case '"': // quoted-text
	n = findq("\"", n + 1);
	break;
      case '(': // comment
	for (int nest = 1; nest;) {
	  n = findq("()", n + 1);
	  if (n == string::npos) break;
	  nest += _s[n] == '(' ? 1 : -1;
	}
	break;
      case '.': // dot-atom
	n = findf(" \t\"(),:;<>@[\\]", n + 1);
	if (n != string::npos) --n;
	break;
      case '[': // domain
	n = findq("]", n + 1);
	break;
      case '\\': // escape
	++n;
	break;
      }
      if (n < _s.size()) ++n;
    }
    if (n == string::npos) n = _s.size();
    _next = n;
    if (comment || _s[i] != '(') return _s.substr(i, n - i);
  }
  return string();
}

/*
 * Functions of the class tokenizer
 */
bool
tokenizer::digit(const string& s, int& value)
{
  if (s.empty()) return false;
  char* e;
  value = strtol(s.c_str(), &e, 10);
  return !*e;
}

string
tokenizer::uppercase(string s)
{
  for (string::iterator p = s.begin(); p != s.end(); ++p) {
    *p = static_cast<char>(toupper(*p));
  }
  return s;
}

string::size_type
tokenizer::findq(const char* s, string::size_type pos) const
{
  string delim = s;
  delim += '\\';
  for (; pos < _s.size(); pos += 2) {
    pos = findf(delim.c_str(), pos);
    if (pos == string::npos || _s[pos] != '\\') return pos;
  }
  return string::npos;
}
