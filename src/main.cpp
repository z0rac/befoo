/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "define.h"
#include "mailbox.h"
#include "setting.h"
#include "win32.h"
#include "window.h"
#include <algorithm>
#include <cassert>
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
    DWORD _last;
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
    win32::xlock _key;
    unsigned _fetching;
    list<mailbox*> _fetched;
    void _done(mbox& mb);
    static void _thread(void* param);
  };
  model* model::_model = NULL;
}

model::model()
  : _mailboxes(NULL), _last(GetTickCount()), _fetching(0)
{
  assert(_model == NULL);
  try {
    _load();
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
    int period;
    mb->uripasswd(s["uri"], s.cipher("passwd"));
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
  win32::xlock::up lock(_key);
  unsigned next = 10;
  mbox* fetch = NULL;
  if (_fetching || _fetched.empty()) {
    LOG("Fetch mails..." << endl);
    DWORD elapse = GetTickCount() - _last;
    _last += elapse;
    next = 0;
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
  }
  source.settimer(*this, next);
  for (; fetch; fetch = fetch->fetch) {
    list<mailbox*>::iterator end = _fetched.end();
    if (find(_fetched.begin(), end, fetch) == end) {
      CloseHandle(win32::thread(_thread, (void*)fetch));
      _fetched.push_front(fetch);
      if (_fetching++ == 0) window::broadcast(WM_APP, 0, 0);
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

  win32::xlock::up lock(_key);
  if (--_fetching == 0) {
    LOG("Done all fetching." << endl);
    int recent = 0;
    int unseen = 0;
    for (const mailbox* p = _mailboxes; p; p = p->next()) {
      int n = p->recent();
      if (n > 0) recent += n;
      unseen += p->mails().size();
    }
    window::broadcast(WM_APP, MAKEWPARAM(recent, unseen), LPARAM(&_fetched));
    LOG("***** HEAP SIZE [" << win32::cheapsize() << ", "
	<< win32::heapsize() << "] *****" << endl);
  }
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
    setting(model& model) : _model(model) {}
    void execute(window&) { if (::setting::edit()) PostQuitMessage(1); }
    UINT state(window&) { return _model.fetching() ? MFS_DISABLED : 0; }
  };

  struct exit : public window::command {
    void execute(window&) { LOG("Exit." << endl); PostQuitMessage(0); }
  };
}

namespace {
  void
  patch()
  {
    list<string> mbs = setting::mailboxes();
    for (list<string>::iterator p = mbs.begin(); p != mbs.end(); ++p) {
      setting s = setting::mailbox(*p);
      string uri = s["uri"];
      if (uri.empty()) continue;
      string::size_type i;
      i = uri.find("imaps://");
      if (i != string::npos) uri.replace(i, 5, "imap+ssl");
      i = uri.find("pops://");
      if (i != string::npos) uri.replace(i, 4, "pop+ssl");
      s("uri", uri);
    }
    setting prefs = setting::preferences();
    string s;
    s = prefs["columns"];
    if (!s.empty()) {
      string w = prefs["summary"];
      setting::preferences("summary")
	("window", w)("columns", s);
      prefs.erase("summary").erase("columns");
    }
    s = prefs["mascot"];
    if (!s.empty()) {
      int x, y, tray;
      static_cast<setting::manip>(s)(x = 0)(y = 0)(tray = 0);
      setting::preferences("mascot")
	("position", setting::tuple(x)(y))("tray", tray);
      prefs.erase("mascot");
    }
    s = prefs["notice"];
    if (!s.empty()) {
      int balloon, summary, ac;
      static_cast<setting::manip>(s)(balloon = 10)(summary = 0);
      prefs["autoclose"](ac = 3);
      prefs("balloon", balloon)("summary", setting::tuple(ac)(summary));
      prefs.erase("notice").erase("autoclose");
    }
  }
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
    patch();
    int delay;
    setting::preferences()["delay"](delay = 0);
    for (int qc = 1; qc > 0; delay = 0) {
      auto_ptr<model> m(new model);
      auto_ptr<window> w(mascot());
      w->addcmd(ID_MENU_FETCH, new cmd::fetch(*m));
      w->addcmd(ID_MENU_SUMMARY, new cmd::summary(*m));
      w->addcmd(ID_MENU_SETTINGS, new cmd::setting(*m));
      w->addcmd(ID_MENU_EXIT, new cmd::exit);
      w->settimer(*m, delay > 0 ? delay * 1000 : 1);
      qc = window::eventloop();
      m->cache();
    }
    return 0;
  } catch (...) {}
  return -1;
}
