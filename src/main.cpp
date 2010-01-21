/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "mailbox.h"
#include "setting.h"
#include "winsock.h"
#include "win32.h"
#include "window.h"
#include <algorithm>
#include <cassert>
#include <imagehlp.h>
#include <shlobj.h>
#include <shlwapi.h>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << win32::time(time(NULL)) << "|" << s)
#else
#define DBG(s)
#define LOG(s)
#endif

extern window* mascot();
extern window* summary(const mailbox*);

// model - main model
namespace {
  class model : public window::timer {
    static model* _model;
    struct mbox : public mailbox {
      unsigned period;
      unsigned next;
      string sound;
      mbox* fetch;
      mbox(const string& name)
	: mailbox(name), period(0), next(0) {}
    };
    mailbox* _mailboxes;
    void _load();
    void _release();
    void wakeup(window& source) { fetch(source, false); }
  public:
    model();
    ~model() { _release(); }
    const mailbox* mailboxes() const { return _mailboxes; }
    void cache();
    model& fetch(window& source, bool force = true);
    bool fetching() const { return _fetching != 0; }
  private:
    // the class to control fetching
    win32::mex _key;
    DWORD _last;
    unsigned _fetching;
    list<mailbox*> _fetched;
    int _summary;
    void _done(mbox& mb);
    static void _thread(void* param);
  };
  model* model::_model = NULL;
}

model::model()
  : _mailboxes(NULL), _last(GetTickCount()), _fetching(0), _summary(0)
{
  try {
    _load();
    setting::preferences()["summary"]()(_summary);
  } catch (...) {
    _release();
    throw;
  }
  _model = this;
}

void
model::_release()
{
  for (mailbox* p = _mailboxes; p;) {
    mailbox* next = p->next();
    delete p;
    p = next;
  }
  _mailboxes = NULL;
}

void
model::_load()
{
  mailbox* last = NULL;
  list<string> mbs = setting::mailboxes();
  for (list<string>::iterator p = mbs.begin(); p != mbs.end(); ++p) {
    string name = *p;
    LOG("Load mailbox [" << name << "]" << endl);
    setting s = setting::mailbox(name);
    mbox* mb = new mbox(name);
    auto_ptr<mbox> hold(mb);
    int ip;
    s["ip"](ip = 0);
    mb->uripasswd(s["uri"], s.cipher("passwd"),
		  ip == 4 ? AF_INET : ip == 6 ? AF_INET6 : AF_UNSPEC);
    int period;
    s["period"](period = 15);
    s["sound"].sep(0)(mb->sound);
    mb->period = period > 0 ? period * 60000U : 0;
    mb->ignore(setting::cache(mb->uristr()));
    hold.release();
    if (last) last = last->next(mb);
    else last = _mailboxes = mb;
  }
  setting::cacheclear();
}

void
model::cache()
{
  for (mailbox* p = _mailboxes; p; p = p->next()) {
    setting::cache(p->uristr(), p->ignore());
  }
}

model&
model::fetch(window& source, bool force)
{
  win32::mex::trylock lock(_key);
  if (!lock) {
    source.settimer(*this, 1); // delay to fetch.
    return *this;
  }
  LOG("Fetch mails..." << endl);
  unsigned next = 0;
  mbox* fetch = NULL;
  DWORD elapse = GetTickCount() - _last;
  _last += elapse;
  for (mailbox* p = _mailboxes; p; p = p->next()) {
    mbox* mb = static_cast<mbox*>(p);
    if (!mb->period) continue; // No fetching by the setting.
    if (force || mb->next <= elapse) {
      unsigned late = force ? 0 : (elapse - mb->next) % mb->period;
      mb->next = mb->period - late;
      mb->fetch = fetch, fetch = mb;
    } else {
      mb->next -= elapse;
    }
    if (!next || next > mb->next) next = mb->next;
  }
  source.settimer(*this, next);
  for (; fetch; fetch = fetch->fetch) {
    list<mailbox*>::iterator end = _fetched.end();
    if (find(_fetched.begin(), end, fetch) == end) {
      CloseHandle(win32::thread(_thread, (void*)fetch));
      if (_fetching++ == 0) window::broadcast(WM_APP, 0, 0);
      _fetched.push_front(fetch);
    }
  }
  return *this;
}

void
model::_done(mbox& mb)
{
  if (mb.recent() > 0 && !mb.sound.empty()) {
    LOG("Sound: " << mb.sound << endl);
    const char* name = mb.sound.c_str();
    PlaySound(name, NULL, SND_NODEFAULT | SND_NOWAIT | SND_ASYNC|
	      (*PathFindExtension(name) ? SND_FILENAME : SND_ALIAS));
  }

  win32::mex::lock lock(_key);
  if (--_fetching) return;

  LOG("Done all fetching." << endl);
  int recent = 0;
  int unseen = 0;
  for (const mailbox* p = _mailboxes; p; p = p->next()) {
    int n = p->recent();
    if (n > 0) recent += n;
    unseen += p->mails().size();
  }
  window::broadcast(WM_APP, MAKEWPARAM(recent, unseen), LPARAM(&_fetched));
  if (recent && _summary) {
    list<mailbox*>::const_iterator p = _fetched.begin();
    while (p != _fetched.end() && (*p)->recent() <= 0) ++p;
    if (p != _fetched.end()) {
      window::broadcast(WM_COMMAND, MAKEWPARAM(0, ID_MENU_SUMMARY), 0);
    }
  }
  _fetched.clear();
  LOG("***** HEAP SIZE [" << win32::cheapsize() << ", "
      << win32::heapsize() << "] *****" << endl);
}

void
model::_thread(void* param)
{
  mbox& mb = *reinterpret_cast<mbox*>(param);
  LOG("Start thread [" << mb.name() << "]." << endl);
  try {
    mb.fetchmail();
  } catch (const exception& DBG(e)) {
    LOG(e.what() << endl);
  } catch (...) {
    LOG("Unknown exception." << endl);
  }
  _model->_done(mb);
  LOG("End thread [" << mb.name() << "]." << endl);
}

#if USE_REG
/** repository - registory added some features.
 */
#define REG_KEY "Software\\GNU\\" APP_NAME
namespace {
  class repository : public registory {
    static bool _appendix(const char* file, char* path);
  public:
    repository() : registory(REG_KEY) {}
    bool edit();
  };
}

bool
repository::edit()
{
  LOG("Edit setting." << endl);

  struct event {
    HANDLE h;
    event() : h(win32::valid(CreateEvent(NULL, FALSE, FALSE, NULL))) {}
    ~event() { CloseHandle(h); }
  } event;

  struct key {
    HKEY h;
    key(const char* key)
    { if (RegOpenKeyEx (HKEY_CURRENT_USER, key,
			0, KEY_NOTIFY, &h) != ERROR_SUCCESS) throw win32::error(); }
    ~key() { RegCloseKey(h); }
  } key(REG_KEY);

  if (RegNotifyChangeKeyValue(key.h, TRUE,
			      REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
			      event.h, TRUE) != ERROR_SUCCESS) throw win32::error();
  char s[MAX_PATH];
  if (_appendix("extend.dll", s) && GetShortPathName(s, s, sizeof(s)) < sizeof(s)) {
    HANDLE h = win32::shell(string("rundll32.exe ") + s + ",settingdlg " REG_KEY,
			    SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT);
    if (h) {
      WaitForSingleObject(h, INFINITE);
      CloseHandle(h);
    }
  }
  return WaitForSingleObject(event.h, 0) == WAIT_OBJECT_0;
}
#else // !USE_REG
/** repository - profile added some features.
 */
#define INI_FILE APP_NAME ".ini"
namespace {
  class repository : public profile {
    static string _path;
    static bool _appendix(const char* file, char* path);
    static const char* _prepare();
  public:
    repository() : profile(_prepare()) {}
    bool edit();
  };
  string repository::_path;
}

const char*
repository::_prepare()
{
  char path[MAX_PATH];
  if (_appendix(INI_FILE, path) ||
      SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
		      NULL, SHGFP_TYPE_CURRENT, path) == 0 &&
      PathAppend(path, APP_NAME "\\" INI_FILE) &&
      MakeSureDirectoryPathExists(path)) {
    LOG("Using the setting file: " << path << endl);
    _path = path;
    HANDLE h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
			  NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    return _path.c_str();
  }
  return NULL;
}

bool
repository::edit()
{
  LOG("Edit setting." << endl);
  if (_path.empty()) return false;

  WritePrivateProfileString(NULL, NULL, NULL, _path.c_str()); // flush entries.
  HANDLE fh = CreateFile(_path.c_str(), GENERIC_READ,
			 FILE_SHARE_READ | FILE_SHARE_WRITE,
			 NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (fh == INVALID_HANDLE_VALUE) throw win32::error();

  FILETIME before = { 0 };
  GetFileTime(fh, NULL, NULL, &before);
  FILETIME after = before;
  char s[MAX_PATH];
  HANDLE h = win32::shell(_appendix("extend.dll", s) &&
			  GetShortPathName(s, s, sizeof(s)) < sizeof(s) ?
			  string("rundll32.exe ") + s + ",settingdlg " + _path :
			  '"' + _path + '"',
			  SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT);
  if (h) {
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    GetFileTime(fh, NULL, NULL, &after);
  }
  CloseHandle(fh);

  return CompareFileTime(&before, &after) != 0;
}
#endif // !USE_REG

bool
repository::_appendix(const char* file, char* path)
{
  return GetModuleFileName(NULL, path, MAX_PATH) < MAX_PATH &&
    PathRemoveFileSpec(path) && PathAppend(path, file) &&
    PathFileExists(path);
}

namespace cmd {
  struct fetch : public window::command {
    model& _model;
    fetch(model& model) : _model(model) {}
    void execute(window& source) { _model.fetch(source); }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct summary : public window::command {
    model& _model;
    auto_ptr<window> _summary;
    summary(model& model) : _model(model) {}
    void execute(window&)
    {
      LOG("Open the summary window." << endl);
      _summary.reset(); // to save preferences
      _summary.reset(::summary(_model.mailboxes()));
    }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct setting : public window::command {
    model& _model;
    repository& _rep;
    setting(model& model, repository& rep) : _model(model), _rep(rep) {}
    void execute(window&) { if (_rep.edit()) PostQuitMessage(1); }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct exit : public window::command {
    void execute(window&) { LOG("Exit." << endl); PostQuitMessage(0); }
  };

  struct logoff : public window::command {
    model& _model;
    logoff(model& model) : _model(model) {}
    void execute(window&) { _model.cache(); }
  };
}

/*
 * WinMain - main function
 */
#if defined(_DEBUG) && defined(_MSC_VER)
int main()
#else
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
#endif
{
  try {
    static win32 befoo("befoo:79585F30-DD15-446C-B414-152D31324970");
    static winsock winsock;
    static repository rep;
    int delay;
    setting::preferences()["delay"](delay = 0);
    for (int qc = 1; qc > 0; delay = 0) {
      auto_ptr<model> m(new model);
      auto_ptr<window> w(mascot());
      w->addcmd(ID_MENU_FETCH, new cmd::fetch(*m));
      w->addcmd(ID_MENU_SUMMARY, new cmd::summary(*m));
      w->addcmd(ID_MENU_SETTINGS, new cmd::setting(*m, rep));
      w->addcmd(ID_MENU_EXIT, new cmd::exit);
      w->addcmd(ID_EVENT_LOGOFF, new cmd::logoff(*m));
      w->settimer(*m, delay > 0 ? delay * 1000 : 1);
      qc = window::eventloop();
      m->cache();
    }
    return 0;
  } catch (...) {}
  return -1;
}
