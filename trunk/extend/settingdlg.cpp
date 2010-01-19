/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "win32.h"
#include "setting.h"
#include "mailbox.h"
#include <cassert>
#include <map>
#include <windowsx.h>
#include <commctrl.h>
#include <shlwapi.h>

namespace extend { extern win32::module dll; }

/** dialog - base dialog class
 */
namespace {
  class dialog {
    HWND _hwnd;
    mutable HWND _tips;
    mutable bool _balloon;
    static BOOL CALLBACK _dlgproc(HWND h, UINT m, WPARAM w, LPARAM l);
  protected:
    struct textbuf {
      char* data;
      textbuf() : data(NULL) {}
      ~textbuf() { delete [] data; }
      char* operator()(size_t n)
      {
	assert(n);
	delete [] data, data = NULL;
	return data = new char[n];
      }
    };
    virtual void initialize() {}
    virtual void done(bool ok);
    virtual bool action(int id, int cmd);
  public:
    dialog() : _hwnd(NULL), _tips(NULL), _balloon(false) {}
    virtual ~dialog() {}
    HWND hwnd() const { return _hwnd; }
    HWND item(int id) const { return GetDlgItem(_hwnd, id); }
    void enable(int id, bool en) const { EnableWindow(item(id), en); }
    void settext(int id, const string& text) const
    { SetDlgItemText(_hwnd, id, text.c_str()); }
    void setspin(int id, int value, int minv = 0, int maxv = UD_MAXVAL);
    int getint(int id) const { return GetDlgItemInt(_hwnd, id, NULL, FALSE); }
    string gettext(int id) const;
    string getfile(int filter, bool quote = false,
		   const string& dir = string()) const;
    string listitem(int id) const;
    void editselect(int id, int start = 0, int end = -1) const;
    int msgbox(const string& msg, UINT flags = 0) const;
    void balloon(int id, const string& msg) const;
    void clearballoon() const;
    void error(int id, const string& msg, int start = 0, int end = -1) const;
    int modal(int id, HWND parent);
  };
}

BOOL CALLBACK
dialog::_dlgproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  try {
    if (m == WM_INITDIALOG) {
      dialog* dp = reinterpret_cast<dialog*>(l);
      dp->_hwnd = h;
      SetWindowLongPtr(h, GWLP_USERDATA, LONG_PTR(dp));
      dp->initialize();
      return TRUE;
    }
    dialog* dp = reinterpret_cast<dialog*>(GetWindowLongPtr(h, GWLP_USERDATA));
    if (dp && m == WM_COMMAND) {
      dp->clearballoon();
      return dp->action(GET_WM_COMMAND_ID(w, l), GET_WM_COMMAND_CMD(w, l));
    }
  } catch (...) {}
  return FALSE;
}

void
dialog::done(bool ok)
{
  EndDialog(hwnd(), int(ok));
}

bool
dialog::action(int id, int)
{
  switch (id) {
  case IDOK:
  case IDCANCEL:
    done(id == IDOK);
    return true;
  }
  return false;
}

void
dialog::setspin(int id, int value, int minv, int maxv)
{
  HWND h = item(id);
  SendMessage(h, UDM_SETRANGE, 0, MAKELPARAM(maxv, minv));
  SendMessage(h, UDM_SETPOS, 0, value);
}

string
dialog::gettext(int id) const
{
  textbuf buf;
  int size = 0;
  for (int n = 0; n <= size + 1;) {
    n += 256;
    size = GetDlgItemText(_hwnd, id, buf(n), n);
  }
  return string(buf.data, size);
}

string
dialog::getfile(int filter, bool quote, const string& dir) const
{
  char fn[MAX_PATH] = "";
  OPENFILENAME ofn = { sizeof(OPENFILENAME) };
  ofn.hwndOwner = _hwnd;
  string fs = extend::dll.text(filter);
  if (fs.size() > 1) {
    string::reverse_iterator p = fs.rbegin();
    for (char delim = *p; p != fs.rend(); ++p) {
      if (*p == delim) *p = '\0';
    }
    ofn.lpstrFilter = fs.c_str();
    ofn.nFilterIndex = 1;
  }
  ofn.lpstrFile = fn;
  ofn.nMaxFile = MAX_PATH;
  if (!dir.empty()) ofn.lpstrInitialDir = dir.c_str();
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
  if (GetOpenFileName(&ofn)) {
    if (quote) PathQuoteSpaces(fn);
  } else {
    fn[0] = '\0';
  }
  return fn;
}

string
dialog::listitem(int id) const
{
  HWND h = item(id);
  int i = ListBox_GetCurSel(h);
  int n = ListBox_GetTextLen(h, i);
  textbuf buf;
  return n > 0 && ListBox_GetText(h, i, buf(n + 1)) == n ?
    string(buf.data, n) : string();
}

void
dialog::editselect(int id, int start, int end) const
{
  HWND h = item(id);
  SetFocus(h);
  Edit_SetSel(h, start, end);
}

int
dialog::msgbox(const string& msg, UINT flags) const
{
  string t[2];
  if (!msg.empty()) {
    string::size_type i = msg.find_first_of(*msg.rbegin()) + 1;
    if (i < msg.size()) {
      t[0] = msg.substr(i, msg.size() - i - 1);
      t[1] = msg.substr(0, i - 1);
    } else {
      t[0] = msg;
    }
  }
  return MessageBox(_hwnd, t[0].c_str(), t[1].c_str(), flags);
}

void
dialog::balloon(int id, const string& msg) const
{
  TOOLINFO ti = { sizeof(TOOLINFO), TTF_TRACK, _hwnd };
  if (!_tips) {
    _tips = CreateWindow(TOOLTIPS_CLASS, NULL,
			 WS_POPUP | TTS_NOPREFIX | TTS_BALLOON | TTS_CLOSE,
			 CW_USEDEFAULT, CW_USEDEFAULT,
			 CW_USEDEFAULT, CW_USEDEFAULT,
			 _hwnd, NULL, extend::dll, NULL);
    if (_tips) SendMessage(_tips, TTM_ADDTOOL, 0, LPARAM(&ti));
  }
  if (_tips) {
    ti.lpszText = LPSTR(msg.c_str());
    SendMessage(_tips, TTM_UPDATETIPTEXT, 0, LPARAM(&ti));
    RECT r;
    GetWindowRect(item(id), &r);
    SendMessage(_tips, TTM_TRACKPOSITION, 0,
		MAKELPARAM((r.left + r.right) / 2, (r.top + r.bottom) / 2));
    SendMessage(_tips, TTM_TRACKACTIVATE, TRUE, LPARAM(&ti));
    _balloon = true;
  }
  MessageBeep(MB_OK);
}

void
dialog::clearballoon() const
{
  if (_balloon) {
    TOOLINFO ti = { sizeof(TOOLINFO), 0, _hwnd };
    SendMessage(_tips, TTM_TRACKACTIVATE, FALSE, LPARAM(&ti));
    _balloon = false;
  }
}

void
dialog::error(int id, const string& msg, int start, int end) const
{
  editselect(id, start, end);
  balloon(id, msg);
  throw FALSE;
}

int
dialog::modal(int id, HWND parent)
{
  return DialogBoxParam(extend::dll, MAKEINTRESOURCE(id),
			parent, &_dlgproc, LPARAM(this));
}

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
  for (int i = 0; i < sizeof(_proto) / sizeof(_proto[0]); ++i) {
    ComboBox_AddString(item(IDC_COMBO_SCHEME), _proto[i].scheme);
  }
  ComboBox_SetCurSel(item(IDC_COMBO_SCHEME), 0);
  _changeproto(0);

  for (int i = 0; i < sizeof(_proto) / sizeof(_proto[0]); ++i) {
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
  int ip = 0;
  int fetch = 15;
  string sound;
  if (!_name.empty()) {
    settext(IDC_EDIT_NAME, _name);
    setting s = setting::mailbox(_name);
    settext(IDC_COMBO_SERVER, s["uri"]);
    settext(IDC_EDIT_PASSWD, s.cipher("passwd"));
    s["ip"](ip);
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
  for (int i = 0; i < sizeof(uri) / sizeof(uri[0]); ++i) {
    ComboBox_InsertString(item(IDC_COMBO_SERVER), -1, uri[i]);
  }
  list<string> ipvs(extend::dll.texts(IDS_LIST_IP_VERSION));
  for (list<string>::iterator p = ipvs.begin(); p != ipvs.end(); ++p) {
    ComboBox_AddString(item(IDC_COMBO_IP_VERSION), p->c_str());
  }
  ComboBox_SetCurSel(item(IDC_COMBO_IP_VERSION), ip == 4 ? 1 : ip == 6 ? 2 : 0);
  setspin(IDC_SPIN_FETCH, fetch);
  settext(IDC_COMBO_SOUND, _sound(sound));
  for (map<string, string>::iterator p = _snd.begin(); p != _snd.end(); ++p) {
    ComboBox_InsertString(item(IDC_COMBO_SOUND), -1, p->first.c_str());
  }
  static const char* const mua[] = {
    "https://www.gmail.com/",
    "http://mail.live.com/",
    "\"%ProgramFiles%\\Outlook Express\\msimn.exe\" /mail",
    "gnudoitw.exe (wl)(wl-folder-goto-folder-subr \\\"%inbox\\\")"
  };
  for (int i = 0; i < sizeof(mua) / sizeof(mua[0]); ++i) {
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
      const char* np = name.c_str();
      const char* p = StrChr(np, ']');
      if (p) {
	int n = MultiByteToWideChar(GetACP(), 0, np, p - np, NULL, 0);
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
    s("ip", "\0\4\6"[max(ComboBox_GetCurSel(item(IDC_COMBO_IP_VERSION)), 0)]);
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
      if (!_name.empty()) setting::mailbox(_name).erase(NULL);
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
    DWORD operator()(char* p, DWORD n) const
    { return h && RegQueryValueEx(h, NULL, NULL, NULL,
				  LPBYTE(p), &n) == ERROR_SUCCESS ? n : 0; }
    DWORD operator()(DWORD i, char* p, DWORD n) const
    { return h && RegEnumKeyEx(h, i, p, &n, NULL,
			       NULL, NULL, NULL) == ERROR_SUCCESS ? n : 0; }
  };
  key schemes("AppEvents\\Schemes\\Apps\\.Default");
  string disp = snd;
  for (DWORD i = 0;; ++i) {
    char s[256];
    string name(s, schemes(i, s, sizeof(s)));
    if (name.empty()) break;
    if (key(name + "\\.Current", schemes)(s, sizeof(s)) > 1 &&
	key("AppEvents\\EventLabels\\" + name)(s, sizeof(s)) > 1) {
      _snd[s] = name;
      if (name == snd) disp = s;
    }
  }
  return disp;
}

/** maindlg - main dialog
 */
namespace {
  class maindlg : public dialog {
    void initialize();
    void done(bool ok);
    bool action(int id, int cmd);
    void _mailbox(bool edit = false);
    void _delete();
    void _enablebuttons();
    void _enableicon(bool en);
  };
}

void
maindlg::initialize()
{
  list<string> mboxes = setting::mailboxes();
  list<string>::iterator p = mboxes.begin();
  for (; p != mboxes.end(); ++p) {
    ListBox_AddString(item(IDC_LIST_MAILBOX), p->c_str());
  }
  _enablebuttons();

  setting pref(setting::preferences());
  int n, b;
  pref["balloon"](n = 10);
  setspin(IDC_SPIN_BALLOON, n);
  pref["summary"](n = 3)(b = 0);
  setspin(IDC_SPIN_SUMMARY, n);
  Button_SetCheck(item(IDC_CHECKBOX_SUMMARY), b);
  pref["delay"](n = 0);
  setspin(IDC_SPIN_STARTUP, n);
  setting::manip v(pref["icon"]);
  b = !string(v).empty();
  v(n = 64);
  setspin(IDC_SPIN_ICON, n, 0, 256);
  Button_SetCheck(item(IDC_CHECKBOX_ICON), b);
  _enableicon(b != 0);
}

void
maindlg::done(bool ok)
{
  if (ok) {
    setting pref(setting::preferences());
    pref("balloon", setting::tuple(getint(IDC_EDIT_BALLOON)));
    pref("summary", setting::tuple(getint(IDC_EDIT_SUMMARY))
	 (Button_GetCheck(item(IDC_CHECKBOX_SUMMARY))));
    pref("delay", setting::tuple(getint(IDC_EDIT_STARTUP)));
    if (Button_GetCheck(item(IDC_CHECKBOX_ICON))) {
      pref("icon", setting::tuple(getint(IDC_EDIT_ICON)));
    } else {
      pref.erase("icon");
    }
  }
  dialog::done(ok);
}

bool
maindlg::action(int id, int cmd)
{
  switch (id) {
  case IDC_BUTTON_NEW:
  case IDC_BUTTON_EDIT:
    _mailbox(id != IDC_BUTTON_NEW);
    return true;
  case IDC_BUTTON_DELETE:
    _delete();
    return true;
  case IDC_LIST_MAILBOX:
    switch (cmd) {
    case LBN_DBLCLK:
      _mailbox(true);
      break;
    case LBN_SELCHANGE:
      _enablebuttons();
      break;
    }
    return true;
  case IDC_CHECKBOX_ICON:
    _enableicon(Button_GetCheck(item(IDC_CHECKBOX_ICON)) != 0);
  }
  return dialog::action(id, cmd);
}

void
maindlg::_mailbox(bool edit)
{
  string name;
  if (edit) {
    name = listitem(IDC_LIST_MAILBOX);
    if (name.empty()) return;
  }
  mailboxdlg dlg(name);
  if (dlg.modal(IDD_MAILBOX, hwnd()) && dlg.name() != name) {
    HWND h = item(IDC_LIST_MAILBOX);
    if (!name.empty()) ListBox_DeleteString(h, ListBox_GetCurSel(h));
    ListBox_AddString(h, dlg.name().c_str());
    ListBox_SetCurSel(h, ListBox_GetCount(h) - 1);
    _enablebuttons();
  }
}

void
maindlg::_delete()
{
  string name = listitem(IDC_LIST_MAILBOX);
  if (!name.empty() &&
      msgbox(extend::dll.textf(IDS_MSGBOX_DELETE, name.c_str()),
	     MB_ICONQUESTION | MB_YESNO) == IDYES) {
    setting::mailbox(name).erase(NULL);
    HWND h = item(IDC_LIST_MAILBOX);
    ListBox_DeleteString(h, ListBox_GetCurSel(h));
    _enablebuttons();
  }
}

void
maindlg::_enablebuttons()
{
  bool en = ListBox_GetCurSel(item(IDC_LIST_MAILBOX)) != LB_ERR;
  enable(IDC_BUTTON_EDIT, en);
  enable(IDC_BUTTON_DELETE, en);
}

void
maindlg::_enableicon(bool en)
{
  enable(IDC_EDIT_ICON, en);
  enable(IDC_SPIN_ICON, en);
}

extern "C" __declspec(dllexport) void settingdlg(HWND, HINSTANCE, LPSTR, int);

void
settingdlg(HWND hwnd, HINSTANCE, LPSTR cmdln, int)
{
  try {
    INITCOMMONCONTROLSEX icce = {
      sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES
    };
    InitCommonControlsEx(&icce);
    profile ini(cmdln);
    maindlg().modal(IDD_SETTING, hwnd);
  } catch (...) {}
}
