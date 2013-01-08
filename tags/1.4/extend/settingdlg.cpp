/*
 * Copyright (C) 2009-2013 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "settingdlg.h"
#include "icon.h"
#include <cassert>
#include <shlwapi.h>
#include <shlobj.h>

/*
 * Functions of the class dialog
 */
INT_PTR CALLBACK
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
    if (dp) {
      switch (m) {
      case WM_COMMAND:
	dp->clearballoon();
	return dp->action(GET_WM_COMMAND_ID(w, l), GET_WM_COMMAND_CMD(w, l));
      case WM_DRAWITEM:
	return dp->drawitem(static_cast<int>(w), LPDRAWITEMSTRUCT(l));
      }
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
  if (id != IDOK && id != IDCANCEL) return false;
  done(id == IDOK);
  return true;
}

bool
dialog::drawitem(int, LPDRAWITEMSTRUCT)
{
  return false;
}

void
dialog::setspin(int id, int value, int minv, int maxv)
{
  HWND h = item(id);
  SendMessage(h, UDM_SETRANGE, 0, MAKELPARAM(maxv, minv));
  SendMessage(h, UDM_SETPOS, 0, value);
}

void
dialog::seticon(int id, HICON icon)
{
  icon = HICON(SendMessage(item(id), BM_SETIMAGE, IMAGE_ICON, LPARAM(icon)));
  if (icon) DestroyIcon(icon);
}

string
dialog::gettext(int id) const
{
  win32::textbuf<char> buf;
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
  win32::textbuf<char> buf;
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
  if (!_balloon) return;
  TOOLINFO ti = { sizeof(TOOLINFO), 0, _hwnd };
  SendMessage(_tips, TTM_TRACKACTIVATE, FALSE, LPARAM(&ti));
  _balloon = false;
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
  return static_cast<int>(DialogBoxParam(extend::dll, MAKEINTRESOURCE(id),
					 parent, &_dlgproc, LPARAM(this)));
}

/** Functions of the class icon spec
 */
iconspec::iconspec(const string& setting, int width)
  : setting(setting), size(64)
{
  try {
    int id;
    string fn;
    (setting::manip(setting))(id = 1).sep(0)(fn);
    icon mascot(id, fn);
    size = mascot.size(), symbol = mascot.read(min(size, width));
  } catch (...) {
    symbol = CopyIcon(LoadIcon(NULL, IDI_QUESTION));
  }
}

/** startup - startup item
 */
namespace {
  class startup {
    template<typename Ty> class com {
      Ty* _p;
    public:
      com() : _p(NULL) {}
      ~com() { _p && _p->Release(); }
      Ty** operator&() { assert(!_p); return &_p; }
      Ty* operator->() const { return _p; }
    };
    string _exe, _link;
  public:
    startup(bool update = false);
    bool exists() const { return !_link.empty(); }
    bool update(bool add);
  };
}

startup::startup(bool update)
{
  win32::textbuf<char> dir(MAX_PATH), path(MAX_PATH);
  if (!GetModuleFileName(NULL, path.data, MAX_PATH)) return;
  _exe = path.data;
  if (!SHGetSpecialFolderPath(NULL, dir.data, CSIDL_STARTUP, FALSE) ||
      !PathCombine(path.data, dir.data, "*")) return;
  for (win32::find fd(path.data); fd; fd.next()) {
    if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
	lstrcmpi(PathFindExtension(fd.cFileName), ".lnk") != 0 ||
	!PathCombine(path.data, dir.data, fd.cFileName)) continue;
    string link = path.data;
    com<IShellLink> sl;
    com<IPersistFile> pf;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
				IID_IShellLink, (PVOID*)&sl)) ||
	FAILED(sl->QueryInterface(IID_IPersistFile, (PVOID*)&pf)) ||
	FAILED(pf->Load(win32::wstr(link), STGM_READ)) ||
	FAILED(sl->GetPath(path.data, MAX_PATH, NULL, 0))) continue;
    if (lstrcmpi(path.data, _exe.c_str()) != 0) {
      if (!*path.data || PathFileExists(path.data) ||
	  FAILED(sl->Resolve(NULL, SLR_NO_UI | SLR_NOSEARCH)) ||
	  FAILED(sl->GetPath(path.data, MAX_PATH, NULL, 0)) ||
	  lstrcmpi(path.data, _exe.c_str()) != 0) continue;
      if (update && SUCCEEDED(pf->Save(NULL, TRUE))) pf->SaveCompleted(NULL);
    }
    _link = link;
    break;
  }
}

bool
startup::update(bool add)
{
  if (!_link.empty() == add) return true;
  if (!add) return DeleteFile(_link.c_str()) != 0;
  com<IShellLink> sl;
  com<IPersistFile> pf;
  if (_exe.empty() ||
      FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
			      IID_IShellLink, (PVOID*)&sl)) ||
      FAILED(sl->QueryInterface(IID_IPersistFile, (PVOID*)&pf)) ||
      FAILED(sl->SetPath(_exe.c_str()))) return false;
  win32::textbuf<char> name(MAX_PATH);
  lstrcpy(name.data, PathFindFileName(LPSTR(_exe.c_str())));
  lstrcpy(PathFindExtension(name.data), ".lnk");
  win32::textbuf<WCHAR> path(MAX_PATH);
  if (!SHGetSpecialFolderPathW(NULL, path.data, CSIDL_STARTUP, TRUE) ||
      !PathAppendW(path.data, win32::wstr(name.data)) ||
      FAILED(pf->Save(path.data, TRUE))) return false;
  pf->SaveCompleted(path.data);
  return true;
}

/** Functions of the class main dialog
 */
void
maindlg::initialize()
{
  { // mailbox list and buttons
    list<string> mboxes = setting::mailboxes();
    list<string>::iterator p = mboxes.begin();
    for (; p != mboxes.end(); ++p) {
      ListBox_AddString(item(IDC_LIST_MAILBOX), p->c_str());
    }
    _enablebuttons();
  }
  setting pref(setting::preferences());
  int n, b, t;
  { // icon group
    pref["icon"](n = 64, b)(t = 0).sep(0)(_icon);
    iconspec icon(_icon, _iconwidth());
    seticon(IDC_BUTTON_ICON, icon.symbol);
    if (!b) n = icon.size;
    setspin(IDC_SPIN_ICON, n, 0, 256);
    Button_SetCheck(item(IDC_CHECKBOX_ICON), b);
    _enableicon(b != 0);
    setspin(IDC_SPIN_ICONTRANS, t, 0, 100);
  }
  // general group
  pref["balloon"](n = 10)(b = 0);
  setspin(IDC_SPIN_BALLOON, n);
  setspin(IDC_SPIN_SUBJECTS, b);
  pref["summary"](n = 3)(b = 0)(t = 0);
  setspin(IDC_SPIN_SUMMARY, n);
  Button_SetCheck(item(IDC_CHECKBOX_SUMMARY), b);
  setspin(IDC_SPIN_SUMMARYTRANS, t, 0, 100);
  pref["delay"](n = 0);
  setspin(IDC_SPIN_STARTUP, n);
  Button_SetCheck(item(IDC_CHECKBOX_STARTUP), startup().exists());
}

void
maindlg::done(bool ok)
{
  if (ok) {
    setting pref(setting::preferences());
    setting::tuple icon
      (Button_GetCheck(item(IDC_CHECKBOX_ICON)) ? gettext(IDC_EDIT_ICON) : "");
    icon(getint(IDC_EDIT_ICONTRANS));
    if (!_icon.empty()) icon(_icon);
    pref("icon", icon);
    pref("balloon", setting::tuple(getint(IDC_EDIT_BALLOON))
	 (getint(IDC_EDIT_SUBJECTS)));
    pref("summary", setting::tuple(getint(IDC_EDIT_SUMMARY))
	 (Button_GetCheck(item(IDC_CHECKBOX_SUMMARY)))
	 (getint(IDC_EDIT_SUMMARYTRANS)));
    pref("delay", setting::tuple(getint(IDC_EDIT_STARTUP)));
    bool add = Button_GetCheck(item(IDC_CHECKBOX_STARTUP)) != 0;
    startup(add).update(add);
  }
  dialog::done(ok);
}

bool
maindlg::action(int id, int cmd)
{
  switch (id) {
  case IDC_BUTTON_NEW:
  case IDC_BUTTON_EDIT:
    mailbox(id != IDC_BUTTON_NEW);
    return true;
  case IDC_BUTTON_DELETE:
    _delete();
    return true;
  case IDC_LIST_MAILBOX:
    switch (cmd) {
    case LBN_DBLCLK: mailbox(true); break;
    case LBN_SELCHANGE: _enablebuttons(); break;
    }
    return true;
  case IDC_BUTTON_ICON:
    icon();
    return true;
  case IDC_CHECKBOX_ICON:
    _enableicon(Button_GetCheck(item(IDC_CHECKBOX_ICON)) != 0);
    return true;
  }
  return dialog::action(id, cmd);
}

void
maindlg::_delete()
{
  string name = listitem(IDC_LIST_MAILBOX);
  if (name.empty() ||
      msgbox(extend::dll.textf(IDS_MSGBOX_DELETE, name.c_str()),
	     MB_ICONQUESTION | MB_YESNO) != IDYES) return;
  setting::mailboxclear(name);
  HWND h = item(IDC_LIST_MAILBOX);
  ListBox_DeleteString(h, ListBox_GetCurSel(h));
  _enablebuttons();
}

void
maindlg::_enablebuttons()
{
  bool en = ListBox_GetCurSel(item(IDC_LIST_MAILBOX)) != LB_ERR;
  enable(IDC_BUTTON_EDIT, en);
  enable(IDC_BUTTON_DELETE, en);
}

int
maindlg::_iconwidth() const
{
  RECT rc;
  GetClientRect(item(IDC_BUTTON_ICON), &rc);
  return min(rc.right, rc.bottom) - 8;
}

void
maindlg::_enableicon(bool en)
{
  enable(IDC_EDIT_ICON, en);
  enable(IDC_SPIN_ICON, en);
}
