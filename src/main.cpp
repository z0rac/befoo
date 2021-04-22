/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#include <thread>
#include <mutex>
#include <imagehlp.h>
#include <shlobj.h>
#include <shlwapi.h>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << win32::time(time({})) << "|" << s)
#else
#define DBG(s)
#define LOG(s)
#endif

extern window* mascot();
extern window* summary(mailbox const*);

// model - main model
namespace {
  class model : public window::timer {
    struct mbox : public mailbox {
      unsigned period = 0;
      unsigned next = 0;
      std::string sound;
      mbox* fetch = {};
      mbox(std::string const& name) : mailbox(name) {}
    };
    mailbox* _mailboxes = {};
    void _release();
    void wakeup(window& source) override { fetch(source, false); }
  public:
    model();
    ~model() { _release(); }
    auto mailboxes() const noexcept { return _mailboxes; }
    void cache();
    model& fetch(window& source, bool force = true);
    bool fetching() const { return _fetching != 0; }
  private:
    // the classes to control fetching
    std::mutex _mutex;
    std::condition_variable _cond;
    DWORD _last = GetTickCount();
    unsigned _fetching = 0;
    std::list<mailbox*> _fetched;
    int _summary = 0;
    void _done(mbox& mb);
  };
}

model::model()
{
  try {
    mailbox* last = {};
    for (auto& name : setting::mailboxes()) {
      LOG("Load mailbox [" << name << "]" << std::endl);
      auto s = setting::mailbox(name);
      auto mb = new mbox(name);
      std::unique_ptr<mbox> hold(mb);
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
      auto cache = setting::cache(mb->uristr());
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
}

void
model::_release()
{
  cache();
  for (auto p = _mailboxes; p;) {
    auto next = p->next();
    delete p;
    p = next;
  }
}

void
model::cache()
{
  std::unique_lock lock(_mutex);
  _cond.wait(lock, [this] { return !_fetching; });
  for (mailbox* p = _mailboxes; p; p = p->next()) {
    setting::cache(p->uristr(), p->ignore());
  }
}

model&
model::fetch(window& source, bool force)
{
  std::unique_lock lock(_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    source.settimer(*this, 1); // delay to fetch.
    return *this;
  }
  LOG("Fetch mails..." << std::endl);
  unsigned next = 0;
  mbox* fetch = {};
  auto elapse = GetTickCount() - _last;
  _last += elapse;
  for (auto p = _mailboxes; p; p = p->next()) {
    auto mb = static_cast<mbox*>(p);
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
    if (auto end = _fetched.cend(); find(_fetched.cbegin(), end, fetch) == end) {
      std::thread([&mb = *fetch, this] {
	LOG("Start thread [" << mb.name() << "]." << std::endl);
	try {
	  mb.fetchmail();
	} catch (std::exception const& DBG(e)) {
	  LOG(e.what() << std::endl);
	} catch (...) {
	  LOG("Unknown exception." << std::endl);
	}
	LOG("End thread [" << mb.name() << "]." << std::endl);
	_done(mb);
      }).detach();
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
    LOG("Sound: " << mb.sound << std::endl);
    auto name = mb.sound.c_str();
    PlaySound(name, {}, SND_NODEFAULT | SND_NOWAIT | SND_ASYNC|
	      (*PathFindExtension(name) ? SND_FILENAME : SND_ALIAS));
  }
  std::unique_lock lock(_mutex);
  if (--_fetching) return;

  LOG("Done all fetching." << std::endl);
  size_t recent = 0, unseen = 0;
  for (auto p = _mailboxes; p; p = p->next()) {
    if (int n = p->recent(); n > 0) recent += n;
    unseen += p->mails().size();
  }
  window::broadcast(WM_APP, MAKEWPARAM(recent, unseen), LPARAM(&_fetched));
  if (recent && _summary) {
    for (auto p : _fetched) {
      if (p->recent() <= 0) continue;
      window::broadcast(WM_COMMAND, MAKEWPARAM(0, ID_MENU_SUMMARY), 0);
      break;
    }
  }
  _fetched.clear();
  _cond.notify_all();
  LOG("***** HEAP SIZE [" << win32::cheapsize() << ", "
      << win32::heapsize() << "] *****" << std::endl);
}

namespace { // misc. functions
  bool appendix(char const* file, char* path) {
    return GetModuleFileName({}, path, MAX_PATH) < MAX_PATH &&
      PathRemoveFileSpec(path) && PathAppend(path, file) &&
      PathFileExists(path);
  }

  bool shell(std::string_view cmd) {
    auto h = win32::shell(cmd, SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT);
    if (!h) return false;
    WaitForSingleObject(h, INFINITE);
    CloseHandle(h);
    return true;
  }

  bool rundll(char const* arg) {
    char s[MAX_PATH];
    if (!appendix("extend.dll", s)) return false;
    win32::dll extend(s);
    if (!extend) return false;
    typedef void (*settingdlg)(HWND, HINSTANCE, LPCSTR, int);
    auto fun = settingdlg(extend("settingdlg", {}));
    if (!fun) return false;
    fun({}, win32::exe, arg, SW_NORMAL);
    return true;
  }
}

/** repository - profile added some features.
 */
#define INI_FILE APP_NAME ".ini"
namespace {
  class repository : public profile {
    static inline std::string _path = [] {
      char path[MAX_PATH];
      if (appendix(INI_FILE, path) ||
	  (SHGetFolderPath({}, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
			   {}, SHGFP_TYPE_CURRENT, path) == 0 &&
	   PathAppend(path, APP_NAME "\\" INI_FILE) &&
	   MakeSureDirectoryPathExists(path))) {
	LOG("Using the setting file: " << path << std::endl);
	auto h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
			    {}, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, {});
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	return std::string(path);
      }
      return std::string();
    }();
  public:
    repository() : profile(!_path.empty() ? _path.c_str() : nullptr) {}
    bool edit();
  };
}

bool
repository::edit()
{
  LOG("Edit setting." << std::endl);
  if (_path.empty()) return false;

  WritePrivateProfileString({}, {}, {}, _path.c_str()); // flush entries.
  auto fh = CreateFile(_path.c_str(), GENERIC_READ,
		       FILE_SHARE_READ | FILE_SHARE_WRITE,
		       {}, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, {});
  if (fh == INVALID_HANDLE_VALUE) throw win32::error();

  FILETIME before {};
  GetFileTime(fh, {}, {}, &before);
  auto after = before;
  (rundll(_path.c_str()) || shell('"' + _path + '"')) &&
    GetFileTime(fh, {}, {}, &after);
  CloseHandle(fh);

  return CompareFileTime(&before, &after) != 0;
}

namespace cmd {
  struct fetch : public window::command {
    model& _model;
    fetch(model& model) : window::command(-265), _model(model) {}
    void execute(window& source) override { _model.fetch(source); }
    UINT state(window&) override { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct summary : public window::command {
    model& _model;
    std::unique_ptr<window> _summary;
    summary(model& model) : window::command(-281), _model(model) {}
    void execute(window&) override {
      LOG("Open the summary window." << std::endl);
      _summary.reset(); // to save preferences
      _summary.reset(::summary(_model.mailboxes()));
    }
    UINT state(window&) override { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct setting : public window::command {
    repository& _rep;
    DWORD _tid = GetCurrentThreadId();
    bool _busy = false;
    setting(repository& rep) : window::command(-274), _rep(rep) {}
    void execute(window&) override {
      if (!_busy) std::thread([this] {
	if (_rep.edit()) PostThreadMessage(_tid, WM_QUIT, 1, 0);
	_busy = false;
      }).detach(), _busy = true;
    }
    UINT state(window&) override { return _busy ? MFS_DISABLED : 0; }
  };

  struct exit : public window::command {
    exit() : window::command(-28) {}
    void execute(window&) override { LOG("Exit." << std::endl); PostQuitMessage(0); }
  };

  struct logoff : public window::command {
    model& _model;
    logoff(model& model) : _model(model) {}
    void execute(window&) override { _model.cache(); }
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
      std::unique_ptr<window> w(mascot());
      std::unique_ptr<model> m(new model);
      w->addcmd(ID_MENU_FETCH, new cmd::fetch(*m));
      w->addcmd(ID_MENU_SUMMARY, new cmd::summary(*m));
      w->addcmd(ID_MENU_SETTINGS, new cmd::setting(rep));
      w->addcmd(ID_MENU_EXIT, new cmd::exit);
      w->addcmd(ID_EVENT_LOGOFF, new cmd::logoff(*m));
      w->settimer(*m, max(delay * 1000, 1));
      qc = window::eventloop();
    }
    return 0;
  } catch (...) {}
  return -1;
}
