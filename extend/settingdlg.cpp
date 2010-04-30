/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "settingdlg.h"
#include "icon.h"
#include <cassert>
#include <shlwapi.h>

/** textbuf - temporary text buffer
 */
namespace {
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
}

/*
 * Functions of the class dialog
 */
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

void
dialog::seticon(int id, HICON icon)
{
  icon = HICON(SendMessage(item(id), BM_SETIMAGE, IMAGE_ICON, LPARAM(icon)));
  if (icon) DestroyIcon(icon);
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

/** icondlg - icon dialog
 */
namespace {
  class icondlg : public dialog {
    int _id;
    string _file;
    int _size;
  public:
    icondlg() : _id(1), _size(64) {}
    int id() const { return _id; }
    const string& file() const { return _file; }
    int size() const { return _size; }
    HICON load(int id, const string& file);
  };
}

HICON
icondlg::load(int id, const string& file)
{
  _id = id, _file = file, _size = 64;
  try {
    const char* fn = file.empty() ? "befoo.exe" : file.c_str();
    char path[MAX_PATH];
    if (GetModuleFileName(extend::dll, path, MAX_PATH) >= MAX_PATH ||
	!PathRemoveFileSpec(path) || !PathCombine(path, path, fn)) throw -1;
    icon mascot(MAKEINTRESOURCE(id), path);
    _size = mascot.size();
    return mascot.symbol(42);
  } catch (...) {
    return CopyIcon(LoadIcon(NULL, IDI_QUESTION));
  }
}

/** maindlg - main dialog
 */
namespace {
  class maindlg : public dialog {
    icondlg _icondlg;
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
  int n, b, t;
  { // initialize icon group
    int id;
    string fn;
    pref["icon"](n = 64, b)(t = 0)(fn)(id = 1);
    seticon(IDC_BUTTON_ICON, _icondlg.load(id, fn));
    if (!b) n = _icondlg.size();
    setspin(IDC_SPIN_ICON, n, 0, 256);
    Button_SetCheck(item(IDC_CHECKBOX_ICON), b);
    _enableicon(b != 0);
  }
  setspin(IDC_SPIN_ICONTRANS, t, 0, 100);
  pref["balloon"](n = 10);
  setspin(IDC_SPIN_BALLOON, n);
  pref["summary"](n = 3)(b = 0)(t = 0);
  setspin(IDC_SPIN_SUMMARY, n);
  Button_SetCheck(item(IDC_CHECKBOX_SUMMARY), b);
  setspin(IDC_SPIN_SUMMARYTRANS, t, 0, 100);
  pref["delay"](n = 0);
  setspin(IDC_SPIN_STARTUP, n);
}

void
maindlg::done(bool ok)
{
  if (ok) {
    setting pref(setting::preferences());
    { // save icon group
      setting::tuple icon
	(Button_GetCheck(item(IDC_CHECKBOX_ICON)) ? gettext(IDC_EDIT_ICON) : "");
      icon(getint(IDC_EDIT_ICONTRANS));
      if (_icondlg.id() != 1 || !_icondlg.file().empty()) {
	icon(_icondlg.file());
	if (_icondlg.id() != 1) icon(_icondlg.id());
      }
      pref("icon", icon);
    }
    pref("balloon", setting::tuple(getint(IDC_EDIT_BALLOON)));
    pref("summary", setting::tuple(getint(IDC_EDIT_SUMMARY))
	 (Button_GetCheck(item(IDC_CHECKBOX_SUMMARY)))
	 (getint(IDC_EDIT_SUMMARYTRANS)));
    pref("delay", setting::tuple(getint(IDC_EDIT_STARTUP)));
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
  extern string editmailbox(const string&, HWND);
  string n = editmailbox(name, hwnd());
  if (n != name) {
    HWND h = item(IDC_LIST_MAILBOX);
    if (!name.empty()) ListBox_DeleteString(h, ListBox_GetCurSel(h));
    ListBox_AddString(h, n.c_str());
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
    setting::mailboxclear(name);
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

extern "C" __declspec(dllexport) void
settingdlg(HWND hwnd, HINSTANCE, LPSTR cmdln, int)
{
  try {
    INITCOMMONCONTROLSEX icce = {
      sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES
    };
    InitCommonControlsEx(&icce);
#if USE_REG
    registory rep(cmdln);
#else
    profile rep(cmdln);
#endif
    maindlg().modal(IDD_SETTING, hwnd);
  } catch (...) {}
}
