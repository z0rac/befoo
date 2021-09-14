/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include <algorithm>

#if _DEBUG >= 2
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** pop3 - pop3 protocol backend
 * This class is a mailbox::backend for POP3 protocol.
 */
class pop3 : public mailbox::backend {
  bool _command(std::string const& cmd, bool ok = true);
  bool _ok(bool ok = true);
  using plist = std::list<std::pair<std::string, std::string>>;
  plist _plist(bool upper = false);
  std::string _headers();
#ifdef _DEBUG
  using backend::read;
  std::string read() {
    std::string line = backend::read();
    LOG("R: " << line << std::endl);
    return line;
  }
#endif
public:
  bool login(uri const& uri, std::string const& passwd) override;
  void logout() override;
  size_t fetch(mailbox& mbox, uri const& uri) override;
};

bool
pop3::login(uri const& uri, std::string const& passwd)
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
  return false;
}

void
pop3::logout()
{
  _command("QUIT");
}

size_t
pop3::fetch(mailbox& mbox, uri const& uri)
{
  auto& ignore = mbox.ignore();
  std::list<std::string> ignored;
  std::list<mail> mails;
  std::list<mail> recents;
  bool recent = uri[uri::fragment] == "recent";
  _command("UIDL");
  plist uidl(_plist());
  for (auto uidp = uidl.begin(); uidp != uidl.end(); ++uidp) {
    auto uid = uidp->second;
    if (find(ignore.begin(), ignore.end(), uid) != ignore.end()) {
      ignored.push_back(uid);
      continue;
    }
    LOG("Fetch mail: " << uid << std::endl);
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
  size_t count = recents.size();
  mails.splice(mails.end(), recents);
  auto lock = mbox.lock();
  mbox.mails(mails);
  mbox.ignore(ignored);
  return count;
}

bool
pop3::_command(std::string const& cmd, bool ok)
{
  write(cmd);
  LOG("S: " << cmd << std::endl);
  return _ok(ok);
}

bool
pop3::_ok(bool ok)
{
  auto line = read();
  auto resp = std::string_view(line).substr(0, line.find(' ')) == "+OK";
  if (ok && !resp) throw mailbox::error(line);
  return resp;
}

pop3::plist
pop3::_plist(bool upper)
{
  plist result;
  for (;;) {
    auto line = read();
    if (!line.empty() && line[0] == '.') {
      line.assign(line, 1, line.size() - 1);
      if (line.empty()) break;
    }
    if (upper) line = tokenizer::uppercase(line);
    std::pair<std::string, std::string> ps;
    auto i = line.find(' ');
    ps.first.assign(line, 0, i);
    if (i != line.npos) {
      i = line.find_first_not_of(' ', i);
      if (i != line.npos) ps.second.assign(line, i, line.size() - i);
    }
    result.push_back(ps);
  }
  return result;
}

std::string
pop3::_headers()
{
  std::string result;
  for (;;) {
    auto line = read();
    if (line.empty()) break;
    if (line[0] != '.') result += line;
    else if (line.size() != 1) result += std::string_view(line).substr(1);
    else return result;
    result += "\015\012";
  }
  while (read() != ".") continue;
  return result;
}

mailbox::backend* backendPOP3() { return new pop3; }
