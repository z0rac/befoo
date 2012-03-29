/*
 * Copyright (C) 2009-2012 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
    HANDLE _idle;
    void _done(mbox& mb);
    static void _thread(void* param);
  };
  model* model::_model = NULL;
}

model::model()
  : _mailboxes(NULL), _last(GetTickCount()), _fetching(0), _summary(0),
    _idle(win32::valid(CreateEvent(NULL, TRUE, TRUE, NULL)))
{
  try {
    mailbox* last = NULL;
    list<string> mbs = setting::mailboxes();
    for (list<string>::iterator p = mbs.begin(); p != mbs.end(); ++p) {
      string name = *p;
      LOG("Load mailbox [" << name << "]" << endl);
      setting s = setting::mailbox(name);
      mbox* mb = new mbox(name);
      auto_ptr<mbox> hold(mb);
      int ip, verify;
      s["ip"](ip = 0);
      s["verify"](verify = 3);
      mb->uripasswd(s["uri"], s.cipher("passwd"))
	.domain(ip == 4 ? AF_INET : ip == 6 ? AF_INET6 : AF_UNSPEC)
	.verify(verify);
      int period;
      s["period"](period = 15);
      s["sound"].sep(0)(mb->sound);
      mb->period = period > 0 ? period * 60000U : 0;
      list<string> cache = setting::cache(mb->uristr());
      mb->ignore(cache);
      hold.release();
      last = last ? last->next(mb) : (_mailboxes = mb);
    }
    setting::cacheclear();
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
  cache();
  for (mailbox* p = _mailboxes; p;) {
    mailbox* next = p->next();
    delete p;
    p = next;
  }
  CloseHandle(_idle);
}

void
model::cache()
{
  WaitForSingleObject(_idle, INFINITE);
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
      if (_fetching++ == 0) {
	ResetEvent(_idle);
	window::broadcast(WM_APP, 0, 0);
      }
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
  size_t recent = 0;
  size_t unseen = 0;
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
  SetEvent(_idle);
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
  LOG("End thread [" << mb.name() << "]." << endl);
  _model->_done(mb);
}

namespace { // misc. functions
  bool appendix(const char* file, char* path)
  {
    return GetModuleFileName(NULL, path, MAX_PATH) < MAX_PATH &&
      PathRemoveFileSpec(path) && PathAppend(path, file) &&
      PathFileExists(path);
  }

  bool shell(const string& cmd)
  {
    HANDLE h = win32::shell(cmd, SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT);
    if (!h) return false;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    return true;
  }

  bool rundll(const char* arg)
  {
    char s[MAX_PATH];
    if (!appendix("extend.dll", s)) return false;
    win32::dll extend(s);
    if (!extend) return false;
    typedef void (*settingdlg)(HWND, HINSTANCE, LPCSTR, int);
    settingdlg fun = settingdlg(extend("settingdlg", NULL));
    if (!fun) return false;
    fun(NULL, win32::exe, arg, SW_NORMAL);
    return true;
  }
}

#if USE_REG
/** repository - registory added some features.
 */
#define REG_KEY "Software\\GNU\\" APP_NAME
namespace {
  class repository : public registory {
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
    { if (RegOpenKeyEx(HKEY_CURRENT_USER, key,
		       0, KEY_NOTIFY, &h) != ERROR_SUCCESS) throw win32::error(); }
    ~key() { RegCloseKey(h); }
  } key(REG_KEY);

  if (RegNotifyChangeKeyValue(key.h, TRUE,
			      REG_NOTIFY_CHANGE_NAME | REG_NOTIFY_CHANGE_LAST_SET,
			      event.h, TRUE) != ERROR_SUCCESS) throw win32::error();
  return rundll(REG_KEY) && WaitForSingleObject(event.h, 0) == WAIT_OBJECT_0;
}
#else // !USE_REG
/** repository - profile added some features.
 */
#define INI_FILE APP_NAME ".ini"
namespace {
  class repository : public profile {
    static string _path;
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
  if (appendix(INI_FILE, path) ||
      (SHGetFolderPath(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
		       NULL, SHGFP_TYPE_CURRENT, path) == 0 &&
       PathAppend(path, APP_NAME "\\" INI_FILE) &&
       MakeSureDirectoryPathExists(path))) {
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
  (rundll(_path.c_str()) || shell('"' + _path + '"')) &&
    GetFileTime(fh, NULL, NULL, &after);
  CloseHandle(fh);

  return CompareFileTime(&before, &after) != 0;
}
#endif // !USE_REG

namespace cmd {
  struct fetch : public window::command {
    model& _model;
    fetch(model& model) : window::command(-265), _model(model) {}
    void execute(window& source) { _model.fetch(source); }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct summary : public window::command {
    model& _model;
    auto_ptr<window> _summary;
    summary(model& model) : window::command(-281), _model(model) {}
    void execute(window&)
    {
      LOG("Open the summary window." << endl);
      _summary.reset(); // to save preferences
      _summary.reset(::summary(_model.mailboxes()));
    }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct setting : public window::command {
    repository& _rep;
    DWORD _tid;
    HANDLE _thread;
    setting(repository& rep) : window::command(-274),
			       _rep(rep), _tid(GetCurrentThreadId()), _thread(NULL) {}
    ~setting() { _thread && WaitForSingleObject(_thread, INFINITE); }
    void execute(window&) { if (!busy()) _thread = win32::thread(edit, (void*)this); }
    UINT state(window&) { return busy() ? MFS_DISABLED : 0; }
    static void edit(void* param)
    {
      setting& self = *reinterpret_cast<setting*>(param);
      if (self._rep.edit()) PostThreadMessage(self._tid, WM_QUIT, 1, 0);
    }
    bool busy()
    {
      if (_thread && WaitForSingleObject(_thread, 0) == WAIT_OBJECT_0) {
	CloseHandle(_thread);
	_thread = NULL;
      }
      return _thread != NULL;
    }
  };

  struct exit : public window::command {
    exit() : window::command(-28) {}
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
      auto_ptr<window> w(mascot());
      auto_ptr<model> m(new model);
      w->addcmd(ID_MENU_FETCH, new cmd::fetch(*m));
      w->addcmd(ID_MENU_SUMMARY, new cmd::summary(*m));
      w->addcmd(ID_MENU_SETTINGS, new cmd::setting(rep));
      w->addcmd(ID_MENU_EXIT, new cmd::exit);
      w->addcmd(ID_EVENT_LOGOFF, new cmd::logoff(*m));
      w->settimer(*m, delay > 0 ? delay * 1000 : 1);
      qc = window::eventloop();
    }
    return 0;
  } catch (...) {}
  return -1;
}
