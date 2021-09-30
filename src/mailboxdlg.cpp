/*
 * Copyright (C) 2010-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "stdafx.h"
#include "settingdlg.h"
#include <map>

/** uridlg - dialog that manipulate URI
 */
namespace {
  class uridlg : public dialog {
    static inline constexpr struct {
      char const* scheme;
      int port;
    } _proto[] = {
      { "imap", 143 }, { "imap+ssl", 993 },
      { "pop", 110 },  { "pop+ssl", 995 },
    };
    static inline constexpr size_t _protoN = sizeof(_proto) / sizeof(_proto[0]);
    ::uri _uri;
    void initialize() override;
    void done(bool ok) override;
    bool action(int id, int cmd) override;
    void _changeproto(unsigned i) noexcept;
  public:
    uridlg(std::string const& uri) : _uri(uri) {}
    std::string uri() const { return _uri; }
  };
}

void
uridlg::initialize()
{
  for (auto& proto : _proto) {
    ComboBox_AddString(item(IDC_COMBO_SCHEME), proto.scheme);
  }
  ComboBox_SetCurSel(item(IDC_COMBO_SCHEME), 0);
  _changeproto(0);

  auto i = 0;
  for (auto& proto : _proto) {
    if (_uri[uri::scheme] == proto.scheme) {
      ComboBox_SetCurSel(item(IDC_COMBO_SCHEME), i);
      _changeproto(i);
      break;
    }
    ++i;
  }
  settext(IDC_EDIT_USER, _uri[uri::user]);
  settext(IDC_EDIT_ADDRESS, _uri[uri::host]);
  if (!_uri[uri::port].empty()) settext(IDC_EDIT_PORT, _uri[uri::port]);
  settext(IDC_EDIT_PATH, _uri[uri::path]);
  Button_SetCheck(item(IDC_CHECKBOX_RECENT), _uri[uri::fragment] == "recent");
}

void
uridlg::done(bool ok)
{
  if (ok) {
    _uri[uri::user] = gettext(IDC_EDIT_USER);
    _uri[uri::host] = gettext(IDC_EDIT_ADDRESS);
    _uri[uri::port] = gettext(IDC_EDIT_PORT);
    _uri[uri::path] = gettext(IDC_EDIT_PATH);
    unsigned i = ComboBox_GetCurSel(item(IDC_COMBO_SCHEME));
    if (i < _protoN) {
      _uri[uri::scheme] = _proto[i].scheme;
      if (getint(IDC_EDIT_PORT) == _proto[i].port) _uri[uri::port].clear();
      _uri[uri::fragment] =
	i > 1 && Button_GetCheck(item(IDC_CHECKBOX_RECENT)) != 0 ? "recent" : "";
    }
  }
  dialog::done(ok);
}

bool
uridlg::action(int id, int cmd)
{
  switch (id) {
  case IDC_COMBO_SCHEME:
    if (cmd == CBN_SELCHANGE) {
      _changeproto(ComboBox_GetCurSel(item(IDC_COMBO_SCHEME)));
    }
    return true;
  }
  return dialog::action(id, cmd);
}

void
uridlg::_changeproto(unsigned i) noexcept
{
  bool recent = false;
  if (i < _protoN) {
    setspin(IDC_SPIN_PORT, _proto[i].port);
    recent = i > 1;
  }
  enable(IDC_CHECKBOX_RECENT, recent);
}

/** mailboxdlg - dialog that manipulate mailbox 
 */
namespace {
  class mailboxdlg : public dialog {
    std::string _name;
    std::map<std::string, std::string> _snd;
    void initialize() override;
    void done(bool ok) override;
    bool action(int id, int cmd) override;
    std::string _env(std::string const& path);
    std::string _sound(std::string const& snd);
  public:
    mailboxdlg(std::string const& name = std::string()) : _name(name) {}
    std::string const& name() const { return _name; }
  };
}

void
mailboxdlg::initialize()
{
  int ip = 0, verify = 3, fetch = 15, immediate = 1;
  std::string sound;
  if (!_name.empty()) {
    settext(IDC_EDIT_NAME, _name);
    setting s = setting::mailbox(_name);
    settext(IDC_COMBO_SERVER, s["uri"]);
    settext(IDC_EDIT_PASSWD, s.cipher("passwd"));
    s["ip"](ip);
    s["verify"](verify);
    s["period"](fetch)(immediate);
    s["sound"].sep(0)(sound);
    std::string mua;
    s["mua"].sep(0)(mua);
    settext(IDC_COMBO_MUA, mua);
  }
  constexpr char const* uris[] = {
    "imap+ssl://username%40domain@imap.gmail.com/",
    "pop+ssl://username@pop3.live.com/"
  };
  for (auto uri : uris) {
    ComboBox_InsertString(item(IDC_COMBO_SERVER), -1, uri);
  }
  for (auto const& s : win32::exe.texts(IDS_LIST_IP_VERSION)) {
    ComboBox_AddString(item(IDC_COMBO_IP_VERSION), s.c_str());
  }
  ComboBox_SetCurSel(item(IDC_COMBO_IP_VERSION), ip == 4 ? 1 : ip == 6 ? 2 : 0);
  for (auto const& s : win32::exe.texts(IDS_LIST_VERIFY)) {
    ComboBox_AddString(item(IDC_COMBO_VERIFY), s.c_str());
  }
  ComboBox_SetCurSel(item(IDC_COMBO_VERIFY), verify);
  setspin(IDC_SPIN_FETCH, fetch);
  Button_SetCheck(item(IDC_CHECKBOX_IMMEDIATE), immediate != 0);
  settext(IDC_COMBO_SOUND, _sound(sound));
  for (auto& t : _snd) {
    ComboBox_InsertString(item(IDC_COMBO_SOUND), -1, t.first.c_str());
  }
  constexpr char const* muas[] = {
    "https://www.gmail.com/",
    "http://mail.live.com/",
    "\"%ProgramFiles%\\Windows Live\\Mail\\wlmail.exe\"",
    "\"%ProgramFiles%\\Outlook Express\\msimn.exe\"",
    "emacsclientw.exe -e (wl) -e \"(wl-folder-goto-folder-subr \\\"%inbox\\\")\"",
    "gnudoitw.exe (wl)(wl-folder-goto-folder-subr \\\"%inbox\\\")"
  };
  for (auto mua : muas) {
    ComboBox_InsertString(item(IDC_COMBO_MUA), -1, win32::xenv(mua).c_str());
  }
}

void
mailboxdlg::done(bool ok)
{
  if (ok) {
    auto name = gettext(IDC_EDIT_NAME);
    if (name.empty()) {
      error(IDC_EDIT_NAME, win32::exe.text(IDS_MSG_ITEM_REQUIRED));
    }
    if (name[0] == '(' && *name.rbegin() == ')') {
      error(IDC_EDIT_NAME, win32::exe.text(IDS_MSG_INVALID_CHAR), 0, 1);
    }
    if (auto n = name.find_first_of(setting::invalidchars()); n != name.npos) {
      error(IDC_EDIT_NAME, win32::exe.text(IDS_MSG_INVALID_CHAR), int(n), int(n) + 1);
    }
    auto s = setting::mailbox(name);
    if (name != _name && !std::string(s["passwd"]).empty()) {
      error(IDC_EDIT_NAME, win32::exe.text(IDS_MSG_NAME_EXISTS));
    }
    auto uri = gettext(IDC_COMBO_SERVER);
    if (uri.empty()) {
      error(IDC_COMBO_SERVER, win32::exe.text(IDS_MSG_ITEM_REQUIRED));
    }
    s("uri", uri);
    s.cipher("passwd", gettext(IDC_EDIT_PASSWD));
    int ip = max(ComboBox_GetCurSel(item(IDC_COMBO_IP_VERSION)), 0);
    if (ip) s("ip", "\0\4\6"[ip]);
    else s.erase("ip");
    int verify = max(ComboBox_GetCurSel(item(IDC_COMBO_VERIFY)), 0);
    if (verify < 3) s("verify", verify);
    else s.erase("verify");
    auto period = setting::tuple(getint(IDC_EDIT_FETCH));
    if (Button_GetCheck(item(IDC_CHECKBOX_IMMEDIATE)) == 0) period(0);
    s("period", period);
    std::string st;
    st = gettext(IDC_COMBO_SOUND);
    if (!st.empty()) {
      auto p = _snd.find(st);
      s("sound", p != _snd.end() ? p->second : _env(st));
    } else s.erase("sound");
    st = gettext(IDC_COMBO_MUA);
    if (!st.empty()) s("mua", _env(st));
    else s.erase("mua");
    if (name != _name) {
      if (!_name.empty()) setting::mailboxclear(_name);
      _name = name;
    }
  }
  dialog::done(ok);
}

bool
mailboxdlg::action(int id, int cmd)
{
  std::string s;
  switch (id) {
  case IDC_BUTTON_SERVER:
    if (uridlg dlg(gettext(IDC_COMBO_SERVER)); dlg.modal(IDD_URI, hwnd())) {
      settext(IDC_COMBO_SERVER, dlg.uri());
    }
    return true;
  case IDC_BUTTON_SOUND:
    s = getfile(IDS_FILTER_SOUND, false, win32::xenv("%SystemRoot%\\Media"));
    if (!s.empty()) settext(IDC_COMBO_SOUND, s);
    return true;
  case IDC_BUTTON_PLAY:
    s = gettext(IDC_COMBO_SOUND);
    if (!s.empty()) {
      auto p = _snd.find(s);
      if (p != _snd.end()) s = p->second;
      PlaySound(s.c_str(), {}, SND_NODEFAULT | SND_NOWAIT | SND_ASYNC |
		(*PathFindExtension(s.c_str()) ? SND_FILENAME : SND_ALIAS));
    }
    return true;
  case IDC_BUTTON_MUA:
    s = getfile(IDS_FILTER_PROGRAM, true, win32::xenv("%ProgramFiles%"));
    if (!s.empty()) settext(IDC_COMBO_MUA, s);
    return true;
  }
  return dialog::action(id, cmd);
}

std::string
mailboxdlg::_env(std::string const& path)
{
  auto s = path;
  constexpr char const* vars[] = {
    "%CommonProgramFiles%", "%CommonProgramFiles(x86)%",
    "%ProgramFiles%", "%ProgramFiles(x86)%",
    "%USERPROFILE%", "%SystemRoot%", "%SystemDrive%"
  };
  for (auto var : vars) {
    auto ev = win32::xenv(var);
    if (ev.empty()) continue;
    decltype(s) d;
    for (size_t t = 0; t < s.size();) {
      auto sp = s.c_str() + t, p = sp;
      do {
	p = StrChrI(p, ev[0]);
      } while (p && StrCmpNI(p, ev.c_str(), static_cast<int>(ev.size())) && *++p);
      size_t n = p ? p - sp : s.size() - t;
      d += s.substr(t, n), t += n;
      if (t < s.size()) d += var, t += ev.size();
    }
    s = d;
  }
  return s;
}

std::string
mailboxdlg::_sound(std::string const& snd)
{
  class key {
    HKEY h = {};
  public:
    key(std::string const& path, HKEY parent = HKEY_CURRENT_USER)
    { RegOpenKeyEx(parent, path.c_str(), 0, KEY_READ, &h); }
    ~key() { if (h) RegCloseKey(h); }
    operator HKEY() const { return h; }
    DWORD operator()(char* p, DWORD n, char const* value = {}) const
    { return h && RegQueryValueEx(h, value, {}, {},
				  LPBYTE(p), &n) == ERROR_SUCCESS && n ? n - 1 : 0; }
    DWORD operator()(DWORD i, char* p, DWORD n) const
    { return h && RegEnumKeyEx(h, i, p, &n, {},
			       {}, {}, {}) == ERROR_SUCCESS ? n : 0; }
  };
  key schemes("AppEvents\\Schemes\\Apps\\.Default");
  auto disp = snd;
  for (DWORD i = 0;; ++i) {
    char s[256];
    std::string name(s, schemes(i, s, sizeof(s)));
    if (name.empty()) break;
    if (name[0] == '.' || !key(name + "\\.Current", schemes)(s, sizeof(s))) continue;
    key event("AppEvents\\EventLabels\\" + name);
    std::string label;
    if (event(s, sizeof(s), "DispFileName") &&
	SHLoadIndirectString(win32::wstring(s).c_str(), LPWSTR(s),
			     sizeof(s) / sizeof(WCHAR), {}) == S_OK) {
      label = win32::string(LPWSTR(s));
    } else if (event(s, sizeof(s))) {
      label = s;
    }
    if (!label.empty()) {
      _snd[label] = name;
      if (name == snd) disp = label;
    }
  }
  return disp;
}

/** Functions of the class main dialog
 */
void
maindlg::mailbox(bool edit)
{
  std::string name;
  if (edit) {
    name = listitem(IDC_LIST_MAILBOX);
    if (name.empty()) return;
  }
  mailboxdlg dlg(name);
  if (!dlg.modal(IDD_MAILBOX, hwnd()) || dlg.name() == name) return;
  auto h = item(IDC_LIST_MAILBOX);
  if (!name.empty()) ListBox_DeleteString(h, ListBox_GetCurSel(h));
  ListBox_AddString(h, dlg.name().c_str());
  ListBox_SetCurSel(h, ListBox_GetCount(h) - 1);
  _enablebuttons();
}
