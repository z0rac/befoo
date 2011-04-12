/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include <algorithm>

#if _DEBUG >= 2
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** pop3 - pop3 protocol backend
 * This class is a mailbox::backend for POP3 protocol.
 */
class pop3 : public mailbox::backend {
  bool _command(const string& cmd, bool ok = true);
  bool _ok(bool ok = true);
  typedef list< pair<string, string> > plist;
  plist _plist(bool upper = false);
  string _headers();
#ifdef _DEBUG
  using backend::read;
  string read()
  {
    string line = backend::read();
    LOG("R: " << line << endl);
    return line;
  }
#endif
public:
  void login(const uri& uri, const string& passwd);
  void logout();
  int fetch(mailbox& mbox, const uri& uri);
};

void
pop3::login(const uri& uri, const string& passwd)
{
  _ok();
  if (_command("CAPA", false)) {
    bool uidl = false, stls = false;
    plist cap(_plist(true));
    plist::iterator p = cap.begin();
    for (; p != cap.end(); ++p) {
      if (p->first == "UIDL") uidl = true;
      else if (p->first == "STLS") stls = true;
    }
    if (!uidl) throw mailbox::error("server not support UIDL command");
    if (stls && !tls()) {
      _command("STLS");
      starttls(uri[uri::host]);
      _command("CAPA");
      cap = _plist(true);
    }
    for (p = cap.begin(); p != cap.end(); ++p) {
      if (p->first == "USER") break;
    }
    if (p == cap.end()) throw mailbox::error("login disabled");
  }
  _command("USER " + uri[uri::user]);
  _command("PASS " + passwd);
}

void
pop3::logout()
{
  _command("QUIT");
}

int
pop3::fetch(mailbox& mbox, const uri& uri)
{
  const list<string>& ignore = mbox.ignore();
  list<string> ignored;
  list<mail> mails;
  list<mail> recents;
  bool recent = uri[uri::fragment] == "recent";
  _command("UIDL");
  plist uidl(_plist());
  plist::iterator uidp = uidl.begin();
  for (; uidp != uidl.end(); ++uidp) {
    string uid = uidp->second;
    if (find(ignore.begin(), ignore.end(), uid) != ignore.end()) {
      ignored.push_back(uid);
      continue;
    }
    LOG("Fetch mail: " << uid << endl);
    _command("TOP " + uidp->first + " 0");
    mail m(uid);
    if (m.header(_headers())) {
      ignored.push_back(uid);
      continue;
    }
    if (recent) {
      ignored.push_back(uid);
      recents.push_back(m);
    } else {
      (mbox.find(uid) ? &mails : &recents)->push_back(m);
    }
  }
  int count = recents.size();
  mails.splice(mails.end(), recents);
  mbox.mails(mails);
  mbox.ignore(ignored);
  return count;
}

bool
pop3::_command(const string& cmd, bool ok)
{
  write(cmd);
  LOG("S: " << cmd << endl);
  return _ok(ok);
}

bool
pop3::_ok(bool ok)
{
  string line = read();
  bool resp = line.substr(0, line.find_first_of(' ')) == "+OK";
  if (ok && !resp) throw mailbox::error(line);
  return resp;
}

pop3::plist
pop3::_plist(bool upper)
{
  plist result;
  for (;;) {
    string line = read();
    if (!line.empty() && line[0] == '.') {
      line.assign(line, 1, line.size() - 1);
      if (line.empty()) break;
    }
    if (upper) line = tokenizer::uppercase(line);
    pair<string, string> ps;
    string::size_type i = line.find_first_of(' ');
    ps.first.assign(line, 0, i);
    if (i != string::npos) {
      i = line.find_first_not_of(' ', i);
      if (i != string::npos) ps.second.assign(line, i, line.size() - i);
    }
    result.push_back(ps);
  }
  return result;
}

string
pop3::_headers()
{
  string result;
  string line = read();
  for (; !line.empty(); line = read()) {
    if (line[0] == '.') {
      if (line.size() == 1) break;
      line.erase(0, 1);
    }
    result += line + "\015\012";
  }
  while (line.size() != 1 || line[0] != '.') line = read();
  return result;
}

mailbox::backend* backendPOP3() { return new pop3; }
