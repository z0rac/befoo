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
#include <thread>
#include <mutex>
#include <imagehlp.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cassert>

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
extern void settingdlg();

// model - main model
namespace {
  class model : public window::timer {
    class mbox : public mailbox {
      model& _model;
      std::condition_variable _cond;
      enum { STOP, RUN, EXIT } _state = STOP;
      bool _idle = false;
      bool _idling = false;
      void _fetch();
      void _fetched(bool idle = false);
      void fetching(bool idle) override;
    public:
      mbox(std::string const& name, model& model) : mailbox(name), _model(model) {}
      ~mbox() { exit(); }
    public:
      unsigned period = 0;
      unsigned cycle = 0;
      unsigned remain = 0;
      std::string sound;
    public:
      auto next() noexcept { return static_cast<mbox*>(mailbox::next()); }
      auto ready() const noexcept { return _state == STOP; }
      auto& idle(bool idle) noexcept { return _idle = idle, *this; }
      void fetch();
      void exit() noexcept;
    };
    mbox* _mailboxes = {};
    void _release() noexcept;
    void wakeup(window& source) override { fetch(source, false); }
  public:
    model();
    ~model() { exit(), _release(); }
    auto mailboxes() const noexcept { return _mailboxes; }
    void exit() noexcept;
    model& fetch(window& source, bool force = true);
    bool fetching() const { return _fetching != 0; }
  private:
    // the classes to control fetching
    std::mutex _mutex;
    DWORD _last = GetTickCount();
    unsigned _fetching = 0;
    std::vector<mailbox*> _fetch;
    int _summary = 0;
    void _done(mbox& mb, bool fetched, bool idling);
  };
}

void
model::mbox::_fetch()
{
  {
    auto lock = mailbox::lock();
    _state = RUN;
    _cond.notify_all();
  }
  LOG("Start thread [" << name() << "]." << std::endl);
  try {
    fetchmail(_idle);
  } catch (std::exception const& DBG(e)) {
    LOG(e.what() << std::endl);
  } catch (...) {
    LOG("Unknown exception." << std::endl);
  }
  LOG("End thread [" << name() << "]." << std::endl);
  _fetched();
}

void
model::mbox::_fetched(bool idle)
{
  std::swap(idle, _idling);
  auto gen = _state != EXIT;
  if (gen) {
    if (recent() > 0 && !sound.empty()) {
      LOG("Sound: " << sound << std::endl);
      auto name = sound.c_str();
      PlaySound(name, {}, SND_NODEFAULT | SND_NOWAIT | SND_ASYNC|
		(*PathFindExtension(name) ? SND_FILENAME : SND_ALIAS));
    }
    try { _model._done(*this, !idle, _idling); } catch (...) {}
  }
  if (_idling) return;
  auto lock = mailbox::lock();
  _state = STOP;
  _cond.notify_all();
  if (gen && idle) window::broadcast(WM_COMMAND, MAKEWPARAM(0, ID_EVENT_RETRY), 0);
}

void
model::mbox::fetching(bool idle)
{
  if (_state == EXIT) throw mailbox::error("EXIT");
  if (idle) _fetched(idle);
}

void
model::mbox::fetch()
{
  auto lock = mailbox::lock();
  std::thread([this] { _fetch(); }).detach();
  _cond.wait(lock, [this] { return _state != STOP; });
}

void
model::mbox::exit() noexcept
{
  auto lock = mailbox::lock();
  if (_state == STOP) return;
  _state = EXIT;
  mailbox::exit();
  _cond.wait(lock, [this] { return _state == STOP; });
}

model::model()
{
  try {
    mailbox* last = {};
    for (auto& name : setting::mailboxes()) {
      LOG("Load mailbox [" << name << "]" << std::endl);
      std::unique_ptr<mbox> mb(new mbox(name, *this));
      auto s = setting::mailbox(name);
      int ip, verify;
      s["ip"](ip = 0);
      s["verify"](verify = 3);
      mb->uripasswd(s["uri"], s.cipher("passwd"))
	.domain(ip == 4 ? AF_INET : ip == 6 ? AF_INET6 : AF_UNSPEC)
	.verify(verify);
      int period, idle;
      s["period"](period = 15)(idle = 1);
      s["sound"].sep(0)(mb->sound);
      mb->cycle = mb->period = period > 0 ? period * 60000U : 0;
      mb->idle(idle != 0);
      auto ignore = setting::cache(mb->uristr());
      mb->ignore(ignore);
      last = last ? last->next(mb.release()) : (_mailboxes = mb.release());
    }
    setting::cacheclear();
    setting::preferences()["summary"]()(_summary);
  } catch (...) {
    _release();
    throw;
  }
}

void
model::_release() noexcept
{
  for (auto p = _mailboxes; p;) {
    auto mbox = p;
    p = p->next();
    delete mbox;
  }
}

void
model::exit() noexcept
{
  for (auto p = _mailboxes; p; p = p->next()) p->exit();
  _fetching = 0, _fetch.clear();
  for (auto p = _mailboxes; p; p = p->next()) {
    try { setting::cache(p->uristr(), p->ignore()); } catch (...) {}
  }
}

model&
model::fetch(window& source, bool force)
{
  std::unique_lock lock(_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    source.settimer(*this, 5); // delay to fetch.
    return *this;
  }
  LOG("Fetch mails..." << std::endl);
  unsigned next = 0;
  std::vector<mbox*> fetch;
  auto elapse = GetTickCount() - _last;
  _last += elapse;
  for (auto mbox = _mailboxes; mbox; mbox = mbox->next()) {
    if (!mbox->cycle) continue;
    if (force || mbox->remain <= elapse) {
      unsigned late = force ? 0 : (elapse - mbox->remain) % mbox->cycle;
      mbox->remain = mbox->cycle - late;
      if (mbox->cycle < mbox->period) mbox->cycle = min(mbox->cycle << 1, mbox->period);
      if (mbox->ready()) fetch.push_back(mbox);
    } else {
      mbox->remain -= elapse;
    }
    if (!next || next > mbox->remain) next = mbox->remain;
  }
  source.settimer(*this, next);
  if (!fetch.empty()) {
    if (!_fetching) window::broadcast(WM_APP, 0, 0);
    _fetch.insert(_fetch.end(), fetch.cbegin(), fetch.cend());
    _fetching += static_cast<int>(fetch.size());
    for (auto mbox : fetch) mbox->fetch();
  }
  return *this;
}

void
model::_done(mbox& mb, bool fetched, bool idling)
{
  std::unique_lock lock(_mutex);
  if (fetched) {
    --_fetching;
    if (idling) mb.cycle = 0;
    else if (mb.recent() >= 0) mb.cycle = mb.period;
  } else if (idling) {
    auto e = _fetch.cend();
    if (find(_fetch.cbegin(), e, &mb) == e) _fetch.push_back(&mb);
  } else {
    mb.cycle = 1000, mb.remain = 0;
  }
  if (_fetching) return;
  LOG("Report fetched." << std::endl);
  size_t recent = 0, unseen = 0;
  for (auto p = _mailboxes; p; p = p->next()) {
    if (int n = p->recent(); n > 0) recent += n;
    unseen += p->mails().size();
  }
  auto summary = false;
  if (recent && _summary) {
    for (auto p : _fetch) summary = summary || p->recent() > 0;
  }
  _fetch.push_back({});
  window::broadcast(WM_APP, MAKEWPARAM(recent, unseen), LPARAM(_fetch.data()));
  _fetch.clear();
  if (summary) window::broadcast(WM_COMMAND, MAKEWPARAM(0, ID_MENU_SUMMARY), 0);
}

namespace cmd {
  struct fetch : window::command {
    model& _model;
    fetch(model& model) : window::command(-265), _model(model) {}
    void execute(window& source) override { _model.fetch(source); }
    UINT state(window&) override { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct summary : window::command {
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

  struct settings : window::command {
    DWORD _tid = GetCurrentThreadId();
    bool _busy = false;
    settings() : window::command(-274) {}
    void execute(window&) override {
      if (!_busy) std::thread([this] {
	if (setting::edit()) PostThreadMessage(_tid, WM_QUIT, 1, 0);
	_busy = false;
      }).detach(), _busy = true;
    }
    UINT state(window&) override { return _busy ? MFS_DISABLED : 0; }
  };

  struct exit : window::command {
    exit() : window::command(-28) {}
    void execute(window&) override { LOG("Exit." << std::endl); PostQuitMessage(0); }
  };

  struct logoff : window::command {
    model& _model;
    logoff(model& model) : _model(model) {}
    void execute(window& source) override {
      source.settimer(_model, 0);
      _model.exit();
    }
  };

  struct retry : window::command {
    model& _model;
    retry(model& model) : _model(model) {}
    void execute(window& source) override { _model.fetch(source, false); }
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
    win32 befoo("befoo:79585F30-DD15-446C-B414-152D31324970");
    winsock winsock;
#if USE_REG
    auto rep = setting::registory("Software\\" APP_NAME);
#else
#define INI_FILE APP_NAME ".ini"
    auto rep = setting::profile([] {
      char path[MAX_PATH];
      if (GetModuleFileName({}, path, MAX_PATH) < MAX_PATH &&
	  PathRemoveFileSpec(path) && PathAppend(path, INI_FILE) &&
	  PathFileExists(path)) return std::string(path);
      if (SHGetFolderPath({}, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
			  {}, SHGFP_TYPE_CURRENT, path) == S_OK &&
	  PathAppend(path, APP_NAME "\\" INI_FILE) &&
	  MakeSureDirectoryPathExists(path)) {
	auto h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
			    {}, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, {});
	if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	return std::string(path);
      }
      return std::string();
    }());
#endif
    int delay;
    setting::preferences()["delay"](delay = 0);
    for (int qc = 1; qc > 0; delay = 0) {
      std::unique_ptr<model> m(new model);
      std::unique_ptr<window> w(mascot());
      w->addcmd(ID_MENU_FETCH, new cmd::fetch(*m));
      w->addcmd(ID_MENU_SUMMARY, new cmd::summary(*m));
      w->addcmd(ID_MENU_SETTINGS, new cmd::settings);
      w->addcmd(ID_MENU_EXIT, new cmd::exit);
      w->addcmd(ID_EVENT_LOGOFF, new cmd::logoff(*m));
      w->addcmd(ID_EVENT_RETRY, new cmd::retry(*m));
      w->settimer(*m, max(delay * 1000, 1));
      qc = window::eventloop();
    }
    return 0;
  } catch (...) {}
  return -1;
}
