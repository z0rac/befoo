/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"

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
  assert(_rep && name && name[0]);
  return _rep->storage('(' + std::string(name) + ')');
}

std::list<std::string>
setting::mailboxes()
{
  assert(_rep);
  auto st = _rep->storages();
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

std::string
setting::_cachekey(std::string_view key)
{
  std::string esc;
  auto ch = std::string("$") + _rep->invalidchars();
  for (;;) {
    auto i = key.find_first_of(ch);
    esc += key.substr(0, i);
    if (i == key.npos) break;
    char e[] = "$0";
    e[1] += char(ch.find(key[i]));
    esc += e;
    key = key.substr(i + 1);
  }
  return "(cache:" + esc + ')';
}

std::list<std::string>
setting::cache(std::string_view key)
{
  assert(_rep);
  std::list<std::string> result;
  auto cache = _rep->storage(_cachekey(key));
  for (auto const& k : cache->keys()) result.push_back(cache->get(k));
  return result;
}

void
setting::cache(std::string_view key, std::list<std::string> const& data)
{
  assert(_rep);
  auto id = _cachekey(key);
  _rep->erase(id);
  if (!data.empty()) {
    auto cache = _rep->storage(id);
    auto i = 0;
    for (auto const& v : data) cache->put(win32::digit(++i), v);
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

bool
setting::edit()
{
  assert(_rep);
  auto watch = _rep->watch();
  if (!watch.get()) return false;
  extern void settingdlg();
  settingdlg();
  return watch->changed();
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
  _st->put(key, s);
  return *this;
}

/*
 * Functions of class setting::tuple
 */
setting::tuple&
setting::tuple::add(_str s)
{
  return *this += _sep, *this += s, *this;
}

std::string
setting::tuple::digit(long i)
{
  return win32::digit(i);
}

/*
 * Functions of class setting::manip
 */
std::string_view
setting::manip::next()
{
  constexpr char ws[] = "\t ";
  std::string_view sv(*this);
  auto n = _next;
  if (n < sv.size()) {
    auto i = sv.find_first_not_of(ws, n);
    n = sv.find(_sep, i);
    _next = n != npos ? n + 1 : (n = size());
    if (i < n) return sv.substr(i, sv.find_last_not_of(ws, n) - i + 1);
  }
  return sv.substr(n, 0);
}

bool
setting::manip::next(int& v)
{
  auto s = next();
  if (s.empty()) return false;
  v = strtol(std::string(s).c_str(), {}, 0);
  return true;
}

setting::manip&
setting::manip::operator()(std::string& v)
{
  if (auto s = next(); !s.empty()) v = win32::xenv(std::string(s));
  return *this;
}

std::list<std::string>
setting::manip::split()
{
  std::list<std::string> result;
  while (avail()) result.push_back(win32::xenv(std::string(next())));
  return result;
}

#if USE_REG
/** _registory - implement for setting::storage.
 * This is using Windows registory API.
 */
class setting::_registory : public setting::repository {
  class _regkey {
    HKEY _key = {};
  public:
    _regkey(HKEY key, LPCTSTR name) {
      win32::valid(RegCreateKeyEx(key, name, 0, {}, REG_OPTION_NON_VOLATILE,
				  KEY_ALL_ACCESS, {}, &_key, {}) == ERROR_SUCCESS);
    }
    ~_regkey() { _key && RegCloseKey(_key); }
  public:
    operator HKEY() const noexcept { return _key; }
  };
  _regkey _reg;
public:
  _registory(_str key) : _reg(HKEY_CURRENT_USER, key) {}
public:
  std::unique_ptr<setting::storage> storage(_str name) const override;
  std::list<std::string> storages() const override;
  void erase(_str name) override { _reg && RegDeleteKey(_reg, name); }
  char const* invalidchars() const noexcept override { return "\\"; }
  std::unique_ptr<setting::watch> watch() const override;
};

std::unique_ptr<setting::storage>
setting::_registory::storage(_str name) const
{
  class subkey : public setting::storage {
    _regkey _reg;
  public:
    subkey(HKEY key, char const* name) : _reg(key, name) {}
    std::string get(_str key) const override {
      DWORD type, size;
      if (_reg &&
	  RegQueryValueEx(_reg, key, {}, &type, {}, &size) == ERROR_SUCCESS &&
	  type == REG_SZ) {
	std::unique_ptr<char[]> buf(new char[size]);
	if (RegQueryValueEx(_reg, key, {}, {}, LPBYTE(buf.get()), &size) == ERROR_SUCCESS) {
	  return buf.get();
	}
      }
      return {};
    }
    void put(_str key, _str value) override
    { _reg && RegSetValueEx(_reg, key, 0, REG_SZ, LPCBYTE(value), strlen(value) + 1); }
    void erase(_str key) override { _reg && RegDeleteValue(_reg, key); }
    std::list<std::string> keys() const override {
      std::list<std::string> result;
      if (DWORD size; _reg && RegQueryInfoKey(_reg, {}, {}, {}, {}, {},
					      {}, {}, &size, {}, {}, {}) == ERROR_SUCCESS) {
	std::unique_ptr<char[]> buf(new char[++size]);
	for (DWORD i = 0;; ++i) {
	  auto n = size;
	  if (RegEnumValue(_reg, i, buf.get(), &n, {}, {}, {}, {}) != ERROR_SUCCESS) break;
	  result.push_back(buf.get());
	}
      }
      return result;
    }
  };
  return std::unique_ptr<setting::storage>(new subkey(_reg, name));
}

std::list<std::string>
setting::_registory::storages() const
{
  std::list<std::string> result;
  if (DWORD size; _reg && RegQueryInfoKey(_reg, {}, {}, {}, {}, &size,
					  {}, {}, {}, {}, {}, {}) == ERROR_SUCCESS) {
    std::unique_ptr<char[]> buf(new char[++size]);
    for (DWORD i = 0;; ++i) {
      auto n = size;
      if (RegEnumKeyEx(_reg, i, buf.get(), &n, {}, {}, {}, {}) != ERROR_SUCCESS) break;
      result.push_back(buf.get());
    }
  }
  return result;
}

std::unique_ptr<setting::watch>
setting::_registory::watch() const
{
  if (!_reg) return {};
  class watch : public setting::watch {
    HANDLE _event = win32::valid(CreateEvent({}, false, false, {}));
    HKEY _key = {};
  public:
    watch(HKEY key) {
      if (RegOpenKeyEx(key, {}, 0, KEY_NOTIFY, &_key) == ERROR_SUCCESS) {
	constexpr auto notify = REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET;
	if (RegNotifyChangeKeyValue(_key, true, notify, _event, true) == ERROR_SUCCESS) return;
	RegCloseKey(_key);
      }
      CloseHandle(_event);
      throw win32::error();
    }
    ~watch() { RegCloseKey(_key), CloseHandle(_event); }
    bool changed() const noexcept override
    { return WaitForSingleObject(_event, 0) == WAIT_OBJECT_0; }
  };
  return std::unique_ptr<setting::watch>(new watch(_reg));
}

std::unique_ptr<setting::repository>
setting::registory(_str key)
{
  return std::unique_ptr<repository>(new _registory(key));
}

#else // !USE_REG
/** _profile - implement for setting::repository
 * This is using Windows API for .INI file.
 */
class setting::_profile : public setting::repository {
  std::string _path;
public:
  _profile(_str path) : _path(path) {}
  ~_profile() { win32::profile({}, {}, {}, _path.c_str()); }
public:
  std::unique_ptr<setting::storage> storage(_str name) const override;
  std::list<std::string> storages() const override
  { return setting::manip(win32::profile({}, {}, _path.c_str())).sep(0).split(); }
  void erase(_str name) override { win32::profile(name, {}, {}, _path.c_str()); }
  char const* invalidchars() const noexcept override { return "]"; }
  std::unique_ptr<setting::watch> watch() const override;
};

std::unique_ptr<setting::storage>
setting::_profile::storage(_str name) const
{
  class section : public setting::storage {
    std::string _section;
    char const* _path;
  public:
    section(char const* section, char const* path)
      : _section(section), _path(path) {}
    std::string get(_str key) const override
    { return win32::profile(_section.c_str(), key, _path); }
    void put(_str key, _str value) override {
      std::string v;
      if (value) v.assign(value);
      if (!v.empty() && v[0] == '"' && *v.rbegin() == '"') v = '"' + v + '"';
      if (v != get(key)) win32::profile(_section.c_str(), key, v.c_str(), _path);
    }
    void erase(_str key) override { win32::profile(_section.c_str(), key, {}, _path); }
    std::list<std::string> keys() const override
    { return setting::manip(get({})).sep(0).split(); }
  };
  return std::unique_ptr<setting::storage>(new section(name, _path.c_str()));
}

std::unique_ptr<setting::watch>
setting::_profile::watch() const
{
  if (_path.empty()) return {};
  class watch : public setting::watch {
    HANDLE _h;
    FILETIME _time;
  public:
    watch(LPCSTR path)
      : _h(CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		      {}, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, {})) {
      win32::valid(_h != INVALID_HANDLE_VALUE);
      GetFileTime(_h, {}, {}, &_time);
    }
    ~watch() { CloseHandle(_h); }
    bool changed() const noexcept {
      auto after = _time;
      GetFileTime(_h, {}, {}, &after);
      return CompareFileTime(&_time, &after) != 0;
    }
  };
  WritePrivateProfileString({}, {}, {}, _path.c_str()); // flush entries.
  return std::unique_ptr<setting::watch>(new watch(_path.c_str()));
}

std::unique_ptr<setting::repository>
setting::profile(_str path)
{
  return std::unique_ptr<repository>(new _profile(path));
}
#endif // !USE_REG
