/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "setting.h"
#include "win32.h"
#include <cassert>
#include <ctime>
#include <shlobj.h>

/** profile - implement for setting::repository.
 * This is using Windows API for .INI file.
 */
namespace {
  class profile : public setting::repository {
    static string _path;
    string _section;
    static string _get(const char* section, const char* key);
  public:
    profile(const string& section) : _section(section) {}
    string get(const char* key) const;
    void put(const char* key, const char* value);
    static string sections() { return _get(NULL, NULL); }
    static void path(const string& path) { _path = path; }
  };
  string profile::_path;
}

string
profile::_get(const char* section, const char* key)
{
  return win32::profile(section, key, _path.c_str());
}

string
profile::get(const char* key) const
{
  return _get(_section.c_str(), key);
}

void
profile::put(const char* key, const char* value)
{
  string tmp;
  if (value && value[0] == '"' && value[strlen(value) - 1] == '"') {
    tmp = '"' + string(value) + '"';
    value = tmp.c_str();
  }
  WritePrivateProfileString(_section.c_str(), key, value, _path.c_str());
}

/*
 * Functions of class setting
 */
void
setting::file(const string& path)
{
  profile::path(path);
}

setting
setting::preferences()
{
  // "(preferences)" is the special section name.
  return new profile("(preferences)");
}

setting
setting::preferences(const char* name)
{
  assert(name && name[0]);
  return new profile(string("(") + name + ")");
}

list<string>
setting::mailboxes()
{
  list<string> sect(manip(profile::sections()).sep(0).split());
  for (list<string>::iterator p = sect.begin(); p != sect.end();) {
    // skip sections matched with the pattern "(.*)".
    p =  p->empty() || (*p)[0] == '(' && *p->rbegin() == ')' ? sect.erase(p) : ++p;
  }
  return sect;
}

setting
setting::mailbox(const string& id)
{
  return new profile(id);
}

list<string>
setting::cache(_str key)
{
  list<string> result;
  profile cache(string("(cache:") + key.c_str + ")");
  list<string> keys(manip(cache.get(NULL)).sep(0).split());
  for (list<string>::iterator p = keys.begin(); p != keys.end(); ++p) {
    result.push_back(cache.get(p->c_str()));
  }
  return result;
}

void
setting::cache(_str key, const list<string>& data)
{
  if (!data.empty()) {
    profile cache(string("(cache:") + key.c_str + ")");
    long i = 0;
    for (list<string>::const_iterator p = data.begin(); p != data.end(); ++p) {
      char s[35];
      cache.put(_ltoa(++i, s, 10), p->c_str());
    }
  }
}

static const char code64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string
setting::cipher(_str key)
{
  string s = _rep->get(key);
  if (!s.empty()) {
    if (s[0] == '\x7f') {
      if (s.size() < 5 || (s.size() & 1) == 0) return string();
      string e;
      unsigned i = 0;
      for (const char* p = s.c_str() + 1; *p; p += 2) {
	int c = 0;
	for (int h = 0; h < 2; ++h) {
	  const char* pos = strchr(code64, p[h]);
	  if (!pos) return string();
	  c = (c << 4) | (unsigned(pos - code64) - i) & 15;
	  i += 5 + h;
	}
	e += char(c);
      }
      for (string::size_type i = e.size(); i-- > 2;) e[i] ^= e[i & 1];
      s = e.substr(2);
    } else {
      cipher(key, s);
    }
  }
  return s;
}

setting&
setting::cipher(_str key, const string& value)
{
  union { char s[2]; short r; } seed;
  seed.r = short(unsigned(value.data()) + time(NULL));
  string e = string(seed.s, 2) + value;
  for (string::size_type i = e.size(); i-- > 2;) e[i] ^= e[i & 1];
  string s(1, '\x7f');
  unsigned d = 0;
  for (const char* p = e.c_str(); *p; ++p) {
    s += code64[(((*p >> 4) & 15) + d) & 63];
    s += code64[((*p & 15) + d + 5) & 63];
    d += 11;
  }
  _rep->put(key, s.c_str());
  return *this;
}

/*
 * Functions of class setting::tuple
 */
setting::tuple&
setting::tuple::add(const string& s)
{
  _s += _sep, _s += s;
  return *this;
}

string
setting::tuple::digit(long i)
{
  char s[35];
  return _ltoa(i, s, 10);
}

/*
 * Functions of class setting::mainp
 */
setting::manip::manip(const string& s)
  : _s(s), _next(0), _sep(',') {}

string
setting::manip::next()
{
  if (!avail()) return string();
  string::size_type i = _s.find_first_not_of(" \t", _next);
  if (i == string::npos) {
    _next = _s.size();
    return string();
  }
  _next = _s.find_first_of(_sep, i);
  if (_next == string::npos) _next = _s.size();
  return _s.substr(i, _next++ - i);
}

bool
setting::manip::next(long& v)
{
  string s = next();
  if (s.empty()) return false;
  v = strtol(s.c_str(), NULL, 0);
  return true;
}

setting::manip&
setting::manip::operator()(string& v)
{
  if (avail()) v = win32::xenv(next());
  return *this;
}

list<string>
setting::manip::split()
{
  list<string> result;
  while (avail()) result.push_back(win32::xenv(next()));
  return result;
}
