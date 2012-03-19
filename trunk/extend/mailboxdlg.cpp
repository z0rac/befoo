/*
 * Copyright (C) 2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "settingdlg.h"
#include "mailbox.h"
#include <cassert>
#include <map>
#include <shlwapi.h>

/** uridlg - dialog that manipulate URI
 */
namespace {
  class uridlg : public dialog {
    struct proto {
      const char* scheme;
      int port;
    };
    static const proto _proto[];
    ::uri _uri;
    void initialize();
    void done(bool ok);
    bool action(int id, int cmd);
    void _changeproto(unsigned i);
  public:
    uridlg(const string& uri) : _uri(uri) {}
    string uri() const { return _uri; }
  };
  const uridlg::proto uridlg::_proto[] = {
    { "imap", 143 }, { "imap+ssl", 993 },
    { "pop", 110 },  { "pop+ssl", 995 },
  };
}

void
uridlg::initialize()
{
  for (int i = 0; i < int(sizeof(_proto) / sizeof(_proto[0])); ++i) {
    ComboBox_AddString(item(IDC_COMBO_SCHEME), _proto[i].scheme);
  }
  ComboBox_SetCurSel(item(IDC_COMBO_SCHEME), 0);
  _changeproto(0);

  for (int i = 0; i < int(sizeof(_proto) / sizeof(_proto[0])); ++i) {
    if (_uri[uri::scheme] == _proto[i].scheme) {
      ComboBox_SetCurSel(item(IDC_COMBO_SCHEME), i);
      _changeproto(i);
      break;
    }
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
    if (i < sizeof(_proto) / sizeof(_proto[0])) {
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
uridlg::_changeproto(unsigned i)
{
  bool recent = false;
  if (i < sizeof(_proto) / sizeof(_proto[0])) {
    setspin(IDC_SPIN_PORT, _proto[i].port);
    recent = i > 1;
  }
  enable(IDC_CHECKBOX_RECENT, recent);
}

/** mailboxdlg - dialog that manipulate mailbox 
 */
namespace {
  class mailboxdlg : public dialog {
    string _name;
    map<string, string> _snd;
    void initialize();
    void done(bool ok);
    bool action(int id, int cmd);
    string _env(const string& path);
    string _sound(const string& snd);
  public:
    mailboxdlg(const string& name = string()) : _name(name) {}
    const string& name() const { return _name; }
  };
}

void
mailboxdlg::initialize()
{
  int ip = 0, verify = 3, fetch = 15;
  string sound;
  if (!_name.empty()) {
    settext(IDC_EDIT_NAME, _name);
    setting s = setting::mailbox(_name);
    settext(IDC_COMBO_SERVER, s["uri"]);
    settext(IDC_EDIT_PASSWD, s.cipher("passwd"));
    s["ip"](ip);
    s["verify"](verify);
    s["period"](fetch);
    s["sound"].sep(0)(sound);
    string mua;
    s["mua"].sep(0)(mua);
    settext(IDC_COMBO_MUA, mua);
  }
  static const char* const uri[] = {
    "imap+ssl://username%40domain@imap.gmail.com/",
    "pop+ssl://username@pop3.live.com/"
  };
  for (int i = 0; i < int(sizeof(uri) / sizeof(uri[0])); ++i) {
    ComboBox_InsertString(item(IDC_COMBO_SERVER), -1, uri[i]);
  }
  list<string> ipvs(extend::dll.texts(IDS_LIST_IP_VERSION));
  for (list<string>::iterator p = ipvs.begin(); p != ipvs.end(); ++p) {
    ComboBox_AddString(item(IDC_COMBO_IP_VERSION), p->c_str());
  }
  ComboBox_SetCurSel(item(IDC_COMBO_IP_VERSION), ip == 4 ? 1 : ip == 6 ? 2 : 0);
  list<string> vs(extend::dll.texts(IDS_LIST_VERIFY));
  for (list<string>::iterator p = vs.begin(); p != vs.end(); ++p) {
    ComboBox_AddString(item(IDC_COMBO_VERIFY), p->c_str());
  }
  ComboBox_SetCurSel(item(IDC_COMBO_VERIFY), verify);
  setspin(IDC_SPIN_FETCH, fetch);
  settext(IDC_COMBO_SOUND, _sound(sound));
  for (map<string, string>::iterator p = _snd.begin(); p != _snd.end(); ++p) {
    ComboBox_InsertString(item(IDC_COMBO_SOUND), -1, p->first.c_str());
  }
  static const char* const mua[] = {
    "https://www.gmail.com/",
    "http://mail.live.com/",
    "\"%ProgramFiles%\\Windows Live\\Mail\\wlmail.exe\"",
    "\"%ProgramFiles%\\Outlook Express\\msimn.exe\"",
    "emacsclientw.exe -e (wl) -e \"(wl-folder-goto-folder-subr \\\"%inbox\\\")\"",
    "gnudoitw.exe (wl)(wl-folder-goto-folder-subr \\\"%inbox\\\")"
  };
  for (int i = 0; i < int(sizeof(mua) / sizeof(mua[0])); ++i) {
    ComboBox_InsertString(item(IDC_COMBO_MUA), -1, win32::xenv(mua[i]).c_str());
  }
}

void
mailboxdlg::done(bool ok)
{
  if (ok) {
    string name = gettext(IDC_EDIT_NAME);
    if (name.empty()) {
      error(IDC_EDIT_NAME, extend::dll.text(IDS_MSG_ITEM_REQUIRED));
    }
    if (name[0] == '(' && *name.rbegin() == ')') {
      error(IDC_EDIT_NAME, extend::dll.text(IDS_MSG_INVALID_CHAR), 0, 1);
    }
    {
      int n = StrCSpn(name.c_str(), setting::invalidchars());
      if (string::size_type(n) < name.size()) {
	error(IDC_EDIT_NAME, extend::dll.text(IDS_MSG_INVALID_CHAR), n, n + 1);
      }
    }
    setting s = setting::mailbox(name);
    if (name != _name && !string(s["passwd"]).empty()) {
      error(IDC_EDIT_NAME, extend::dll.text(IDS_MSG_NAME_EXISTS));
    }
    string uri = gettext(IDC_COMBO_SERVER);
    if (uri.empty()) {
      error(IDC_COMBO_SERVER, extend::dll.text(IDS_MSG_ITEM_REQUIRED));
    }
    s("uri", uri);
    s.cipher("passwd", gettext(IDC_EDIT_PASSWD));
    int ip = max(ComboBox_GetCurSel(item(IDC_COMBO_IP_VERSION)), 0);
    if (ip) s("ip", "\0\4\6"[ip]);
    else s.erase("ip");
    int verify = max(ComboBox_GetCurSel(item(IDC_COMBO_VERIFY)), 0);
    if (verify < 3) s("verify", verify);
    else s.erase("verify");
    s("period", setting::tuple(getint(IDC_EDIT_FETCH)));
    string st;
    st = gettext(IDC_COMBO_SOUND);
    if (!st.empty()) {
      map<string, string>::iterator p = _snd.find(st);
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
  string s;
  switch (id) {
  case IDC_BUTTON_SERVER:
    {
      uridlg dlg(gettext(IDC_COMBO_SERVER));
      if (dlg.modal(IDD_URI, hwnd())) {
	settext(IDC_COMBO_SERVER, dlg.uri());
      }
    }
    return true;
  case IDC_BUTTON_SOUND:
    s = getfile(IDS_FILTER_SOUND, false, win32::xenv("%SystemRoot%\\Media"));
    if (!s.empty()) settext(IDC_COMBO_SOUND, s);
    return true;
  case IDC_BUTTON_PLAY:
    s = gettext(IDC_COMBO_SOUND);
    if (!s.empty()) {
      map<string, string>::iterator p = _snd.find(s);
      if (p != _snd.end()) s = p->second;
      PlaySound(s.c_str(), NULL, SND_NODEFAULT | SND_NOWAIT | SND_ASYNC |
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

string
mailboxdlg::_env(const string& path)
{
  string s = path;
  static const char* const vars[] = {
    "%CommonProgramFiles%", "%ProgramFiles%", "%USERPROFILE%",
    "%SystemRoot%", "%SystemDrive%"
  };
  for (unsigned i = 0; i < sizeof(vars) / sizeof(vars[0]); ++i) {
    string ev = win32::xenv(vars[i]);
    if (ev.empty()) continue;
    string d;
    for (string::size_type t = 0; t < s.size();) {
      const char* sp = s.c_str() + t;
      const char* p = sp;
      do {
	p = StrChrI(p, ev[0]);
      } while (p && StrCmpNI(p, ev.c_str(), ev.size()) && *++p);
      string::size_type n = p ? p - sp : s.size() - t;
      d += s.substr(t, n), t += n;
      if (t < s.size()) d += vars[i], t += ev.size();
    }
    s = d;
  }
  return s;
}

string
mailboxdlg::_sound(const string& snd)
{
  class key {
    HKEY h;
  public:
    key(const string& path, HKEY parent = HKEY_CURRENT_USER)
      : h(NULL) { RegOpenKeyEx(parent, path.c_str(), 0, KEY_READ, &h); }
    ~key() { if (h) RegCloseKey(h); }
    operator HKEY() const { return h; }
    DWORD operator()(char* p, DWORD n, const char* value = NULL) const
    { return h && RegQueryValueEx(h, value, NULL, NULL,
				  LPBYTE(p), &n) == ERROR_SUCCESS && n ? n - 1 : 0; }
    DWORD operator()(DWORD i, char* p, DWORD n) const
    { return h && RegEnumKeyEx(h, i, p, &n, NULL,
			       NULL, NULL, NULL) == ERROR_SUCCESS ? n : 0; }
  };
  static win32::dll shlwapi("shlwapi.dll");
  typedef HRESULT (WINAPI* SHLoadIndirectString)(LPCWSTR, LPWSTR, UINT, void**);
  SHLoadIndirectString shload =
    SHLoadIndirectString(shlwapi("SHLoadIndirectString", NULL));
  key schemes("AppEvents\\Schemes\\Apps\\.Default");
  string disp = snd;
  for (DWORD i = 0;; ++i) {
    char s[256];
    string name(s, schemes(i, s, sizeof(s)));
    if (name.empty()) break;
    if (name[0] == '.' || !key(name + "\\.Current", schemes)(s, sizeof(s))) continue;
    key event("AppEvents\\EventLabels\\" + name);
    string label;
    if (shload && event(s, sizeof(s), "DispFileName") &&
	shload(win32::wstr(s), LPWSTR(s), sizeof(s) / sizeof(WCHAR), NULL) == S_OK) {
      label = win32::wstr::mbstr(LPWSTR(s));
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

string
editmailbox(const string& name, HWND parent)
{
  mailboxdlg dlg(name);
  return dlg.modal(IDD_MAILBOX, parent) ? dlg.name() : string();
}
