/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/*
 * Functions of class setting::_repository
 */
static setting::repository* _rep = {};

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
  return _rep->storage('(' + std::string(name) + ')');
}

std::list<std::string>
setting::mailboxes()
{
  assert(_rep);
  std::list<std::string> st(_rep->storages());
  for (auto p = st.begin(); p != st.end();) {
    // skip sections matched with the pattern "(.*)".
    p =  p->empty() || (*p)[0] == '(' && *p->rbegin() == ')' ? st.erase(p) : ++p;
  }
  return st;
}

setting
setting::mailbox(std::string const& id)
{
  assert(_rep);
  return _rep->storage(id);
}

void
setting::mailboxclear(std::string const& id)
{
  assert(_rep);
  _rep->erase(id);
}

namespace {
  std::string cachekey(std::string const& key)
  {
    std::string esc;
    auto ch = std::string("$") + _rep->invalidchars();
    size_t i = 0, n;
    while (n = StrCSpn(key.c_str() + i, ch.c_str()), i + n < key.size()) {
      esc += key.substr(i, n), i += n;
      char e[] = "$0";
      e[1] += char(ch.find(key[i++]));
      esc += e;
    }
    return "(cache:" + esc + key.substr(i, n) + ')';
  }
}

std::list<std::string>
setting::cache(std::string const& key)
{
  assert(_rep);
  std::list<std::string> result;
  std::unique_ptr<storage> cache(_rep->storage(cachekey(key)));
  for (auto const& k : cache->keys()) {
    result.push_back(cache->get(k.c_str()));
  }
  return result;
}

void
setting::cache(std::string const& key, std::list<std::string> const& data)
{
  assert(_rep);
  auto id = cachekey(key);
  _rep->erase(id);
  if (!data.empty()) {
    std::unique_ptr<storage> cache(_rep->storage(id));
    char s[35];
    long i = 0;
    for (auto const& v : data) cache->put(_ltoa(++i, s, 10), v.c_str());
  }
}

void
setting::cacheclear()
{
  assert(_rep);
  for (auto const& key : _rep->storages()) {
    if (!key.empty() && *key.rbegin() == ')' &&
	key.starts_with("(cache:")) _rep->erase(key);
  }
}

char const*
setting::invalidchars()
{
  assert(_rep);
  return _rep->invalidchars();
}

static constexpr std::string_view code64
("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");

std::string
setting::cipher(_str key)
{
  auto s = _st->get(key);
  if (!s.empty()) {
    if (s[0] == '\x7f') {
      if (s.size() < 5 || (s.size() & 1) == 0) return {};
      std::string e;
      unsigned i = 0;
      for (auto p = s.c_str() + 1; *p; p += 2) {
	int c = 0;
	for (int h = 0; h < 2; ++h) {
	  auto pos = code64.find(p[h]);
	  if (pos == code64.npos) return {};
	  c = (c << 4) | ((pos - i) & 15);
	  i += 5 + h;
	}
	e += char(c);
      }
      for (auto j = e.size(); j-- > 2;) e[j] ^= e[j & 1];
      s = e.substr(2);
    } else {
      cipher(key, s);
    }
  }
  return s;
}

setting&
setting::cipher(_str key, std::string const& value)
{
  union { char s[2]; short r; } seed;
  seed.r = short(unsigned(ptrdiff_t(value.data())) + time({}));
  auto e = std::string(seed.s, 2) + value;
  for (auto i = e.size(); i-- > 2;) e[i] ^= e[i & 1];
  std::string s(e.size() * 2 + 1, '\x7f');
  unsigned d = 0;
  for (size_t i = 0; i < e.size(); ++i) {
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
setting::tuple::add(std::string const& s)
{
  _s += _sep, _s += s;
  return *this;
}

std::string
setting::tuple::digit(long i)
{
  char s[35];
  return _ltoa(i, s, 10);
}

/*
 * Functions of class setting::manip
 */
std::string
setting::manip::next()
{
  constexpr char ws[] = "\t ";
  if (!avail()) return {};
  auto i = _s.find_first_not_of(ws, _next);
  auto n = _s.find(_sep, _next);
  _next = n != _s.npos ? n + 1 : (n = _s.size());
  return i < n ? _s.substr(i, _s.find_last_not_of(ws, n - 1) - i + 1) : std::string();
}

bool
setting::manip::next(int& v)
{
  auto s = next();
  if (s.empty()) return false;
  v = strtol(s.c_str(), {}, 0);
  return true;
}

setting::manip&
setting::manip::operator()(std::string& v)
{
  if (auto s = next(); !s.empty()) v = win32::xenv(s);
  return *this;
}

std::list<std::string>
setting::manip::split()
{
  std::list<std::string> result;
  while (avail()) result.push_back(win32::xenv(next()));
  return result;
}

/** section - implement for setting::storage.
 * This is using Windows API for .INI file.
 */
namespace {
  class section : public setting::storage {
    std::string _section;
    char const* _path;
  public:
    section(std::string const& section, char const* path)
      : _section(section), _path(path) {}
    std::string get(char const* key) const override;
    void put(char const* key, char const* value) override;
    void erase(char const* key) override;
    std::list<std::string> keys() const override;
  };
}

std::string
section::get(char const* key) const
{
  return win32::profile(_section.c_str(), key, _path);
}

void
section::put(char const* key, char const* value)
{
  std::string tmp;
  if (value && value[0] == '"' && value[strlen(value) - 1] == '"') {
    tmp = '"' + std::string(value) + '"';
    value = tmp.c_str();
  }
  win32::profile(_section.c_str(), key, value, _path);
}

void
section::erase(char const* key)
{
  win32::profile(_section.c_str(), key, {}, _path);
}

std::list<std::string>
section::keys() const
{
  return setting::manip(get({})).sep(0).split();
}

/*
 * Functions of class profile
 */
profile::~profile()
{
  win32::profile({}, {}, {}, _path.c_str());
}

setting::storage*
profile::storage(std::string const& name) const
{
  return new section(name, _path.c_str());
}

std::list<std::string>
profile::storages() const
{
  return setting::manip(win32::profile({}, {}, _path.c_str())).sep(0).split();
}

void
profile::erase(std::string const& name)
{
  win32::profile(name.c_str(), {}, {}, _path.c_str());
}
