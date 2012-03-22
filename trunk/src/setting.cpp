/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "setting.h"
#include "win32.h"
#include <cassert>
#include <ctime>
#include <shlwapi.h>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of class setting::_repository
 */
static setting::repository* _rep = NULL;

setting::_repository::_repository()
{
  _rep = this;
}

/*
 * Functions of class setting
 */
setting
setting::preferences()
{
  // "(preferences)" is the special section name.
  return _rep->storage("(preferences)");
}

setting
setting::preferences(_str name)
{
  assert(_rep);
  assert(name && name[0]);
  return _rep->storage('(' + string(name) + ')');
}

list<string>
setting::mailboxes()
{
  assert(_rep);
  list<string> st(_rep->storages());
  for (list<string>::iterator p = st.begin(); p != st.end();) {
    // skip sections matched with the pattern "(.*)".
    p =  p->empty() || ((*p)[0] == '(' && *p->rbegin() == ')') ? st.erase(p) : ++p;
  }
  return st;
}

setting
setting::mailbox(const string& id)
{
  assert(_rep);
  return _rep->storage(id);
}

void
setting::mailboxclear(const string& id)
{
  assert(_rep);
  _rep->erase(id);
}

namespace {
  string cachekey(const string& key)
  {
    string esc;
    string ch = string("$") + _rep->invalidchars();
    string::size_type i = 0, n;
    while (n = StrCSpn(key.c_str() + i, ch.c_str()), i + n < key.size()) {
      esc += key.substr(i, n), i += n;
      char e[] = "$0";
      e[1] += char(ch.find_first_of(key[i++]));
      esc += e;
    }
    return "(cache:" + esc + key.substr(i, n) + ')';
  }
}

list<string>
setting::cache(const string& key)
{
  assert(_rep);
  list<string> result;
  auto_ptr<storage> cache(_rep->storage(cachekey(key)));
  list<string> keys(cache->keys());
  for (list<string>::iterator p = keys.begin(); p != keys.end(); ++p) {
    result.push_back(cache->get(p->c_str()));
  }
  return result;
}

void
setting::cache(const string& key, const list<string>& data)
{
  assert(_rep);
  string id = cachekey(key);
  _rep->erase(id);
  if (!data.empty()) {
    auto_ptr<storage> cache(_rep->storage(id));
    long i = 0;
    for (list<string>::const_iterator p = data.begin(); p != data.end(); ++p) {
      char s[35];
      cache->put(_ltoa(++i, s, 10), p->c_str());
    }
  }
}

void
setting::cacheclear()
{
  assert(_rep);
  list<string> rep(_rep->storages());
  for (list<string>::iterator p = rep.begin(); p != rep.end(); ++p) {
    if (!p->empty() && *p->rbegin() == ')' && p->find("(cache:") == 0) {
      _rep->erase(*p);
    }
  }
}

const char*
setting::invalidchars()
{
  assert(_rep);
  return _rep->invalidchars();
}

static const char code64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string
setting::cipher(_str key)
{
  string s = _st->get(key);
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
	  c = (c << 4) | ((unsigned(pos - code64) - i) & 15);
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
  seed.r = short(unsigned(ptrdiff_t(value.data())) + time(NULL));
  string e = string(seed.s, 2) + value;
  for (string::size_type i = e.size(); i-- > 2;) e[i] ^= e[i & 1];
  string s(e.size() * 2 + 1, '\x7f');
  unsigned d = 0;
  for (string::size_type i = 0; i < e.size(); ++i) {
    s[i * 2 + 1] = code64[(((e[i] >> 4) & 15) + d) & 63];
    s[i * 2 + 2] = code64[((e[i] & 15) + d + 5) & 63];
    d += 11;
  }
  _st->put(key, s.c_str());
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
 * Functions of class setting::manip
 */
setting::manip::manip(const string& s)
  : _s(s), _next(0), _sep(',') {}

string
setting::manip::next()
{
  static const char ws[] = " \t";
  if (!avail()) return string();
  string::size_type i = _s.find_first_not_of(ws, _next);
  string::size_type n = _s.find_first_of(_sep, _next);
  _next = n != string::npos ? n + 1 : (n = _s.size());
  return i < n ? _s.substr(i, _s.find_last_not_of(ws, n - 1) - i + 1) : string();
}

bool
setting::manip::next(int& v)
{
  string s = next();
  if (s.empty()) return false;
  v = strtol(s.c_str(), NULL, 0);
  return true;
}

setting::manip&
setting::manip::operator()(string& v)
{
  string s = next();
  if (!s.empty()) v = win32::xenv(s);
  return *this;
}

list<string>
setting::manip::split()
{
  list<string> result;
  while (avail()) result.push_back(win32::xenv(next()));
  return result;
}

#if USE_REG
/** regkey - implement for setting::storage.
 */
namespace {
  class regkey : public setting::storage {
    HKEY _key;
  public:
    regkey(HKEY key, const string& name);
    ~regkey();
    string get(const char* key) const;
    void put(const char* key, const char* value);
    void erase(const char* key);
    list<string> keys() const;
  public:
    struct buf {
      char* data;
      buf(DWORD size) : data(new char[size]) {}
      ~buf() { delete [] data; }
    };
  };
}

regkey::regkey(HKEY key, const string& name)
  : _key(NULL)
{
  key && RegCreateKeyEx(key, name.c_str(), 0, NULL,
			REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &_key, NULL);
}

regkey::~regkey()
{
  _key && RegCloseKey(_key);
}

string
regkey::get(const char* key) const
{
  DWORD type;
  DWORD size;
  if (_key &&
      RegQueryValueEx(_key, key, NULL, &type, NULL, &size) == ERROR_SUCCESS &&
      type == REG_SZ) {
    regkey::buf buf(size);
    if (RegQueryValueEx(_key, key, NULL, NULL, LPBYTE(buf.data), &size) == ERROR_SUCCESS) {
      return buf.data;
    }
  }
  return string();
}

void
regkey::put(const char* key, const char* value)
{
  _key && RegSetValueEx(_key, key, 0, REG_SZ, (const BYTE*)value, strlen(value) + 1);
}

void
regkey::erase(const char* key)
{
  _key && RegDeleteValue(_key, key);
}

list<string>
regkey::keys() const
{
  list<string> result;
  DWORD size;
  if (_key && RegQueryInfoKey(_key, NULL, NULL, NULL, NULL, NULL,
			      NULL, NULL, &size, NULL, NULL, NULL) == ERROR_SUCCESS) {
    regkey::buf buf(++size);
    DWORD i = 0, n;
    while (n = size, RegEnumValue(_key, i++, buf.data, &n,
				  NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
      result.push_back(buf.data);
    }
  }
  return result;
}

/*
 * Functions of class registory
 */
registory::registory(const char* key)
  : _key(NULL)
{
  RegCreateKeyEx(HKEY_CURRENT_USER, key, 0, NULL,
		 REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, PHKEY(&_key), NULL);
}

registory::~registory()
{
  _key && RegCloseKey(HKEY(_key));
}

setting::storage*
registory::storage(const string& name) const
{
  return new regkey(HKEY(_key), name);
}

list<string>
registory::storages() const
{
  list<string> result;
  DWORD size;
  if (_key && RegQueryInfoKey(HKEY(_key), NULL, NULL, NULL, NULL, &size,
			      NULL, NULL, NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
    regkey::buf buf(++size);
    DWORD i = 0, n;
    while (n = size, RegEnumKeyEx(HKEY(_key), i++, buf.data, &n,
				  NULL, NULL, NULL, NULL) == ERROR_SUCCESS) {
      result.push_back(buf.data);
    }
  }
  return result;
}

void
registory::erase(const string& name)
{
  _key && RegDeleteKey(HKEY(_key), name.c_str());
}
#else // !USE_REG
/** section - implement for setting::storage.
 * This is using Windows API for .INI file.
 */
namespace {
  class section : public setting::storage {
    string _section;
    const char* _path;
  public:
    section(const string& section, const char* path)
      : _section(section), _path(path) {}
    string get(const char* key) const;
    void put(const char* key, const char* value);
    void erase(const char* key);
    list<string> keys() const;
  };
}

string
section::get(const char* key) const
{
  return win32::profile(_section.c_str(), key, _path);
}

void
section::put(const char* key, const char* value)
{
  string tmp;
  if (value && value[0] == '"' && value[strlen(value) - 1] == '"') {
    tmp = '"' + string(value) + '"';
    value = tmp.c_str();
  }
  win32::profile(_section.c_str(), key, value, _path);
}

void
section::erase(const char* key)
{
  win32::profile(_section.c_str(), key, NULL, _path);
}

list<string>
section::keys() const
{
  return setting::manip(get(NULL)).sep(0).split();
}

/*
 * Functions of class profile
 */
profile::~profile()
{
  win32::profile(NULL, NULL, NULL, _path);
}

setting::storage*
profile::storage(const string& name) const
{
  return new section(name, _path);
}

list<string>
profile::storages() const
{
  return setting::manip(win32::profile(NULL, NULL, _path)).sep(0).split();
}

void
profile::erase(const string& name)
{
  win32::profile(name.c_str(), NULL, NULL, _path);
}
#endif // !USE_REG
