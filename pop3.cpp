/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
  string _readline();
  typedef list< pair<string, string> > plist;
  plist _plist();
  string _headers();
public:
  void login(const string& user, const string& passwd);
  void logout();
  int fetch(mailbox& mbox, const string& path);
};

void
pop3::login(const string& user, const string& passwd)
{
  _ok();
  _command("USER " + user);
  _command("PASS " + passwd);
}

void
pop3::logout()
{
  _command("QUIT");
}

int
pop3::fetch(mailbox& mbox, const string& path)
{
  const list<string>& ignore = mbox.ignore();
  list<string> ignored;
  list<mail> fetched;
  bool recent = path == "recent";
  int count = 0;
  _command("UIDL");
  plist uidl(_plist());
  plist::iterator uidp = uidl.begin();
  for (; uidp != uidl.end(); ++uidp) {
    string uid = uidp->second;
    if (recent && mbox.find(uid) ||
	find(ignore.begin(), ignore.end(), uid) != ignore.end()) {
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
    fetched.push_back(m);
    count += int(recent || !mbox.find(uid));
  }
  mbox.mails(fetched);
  mbox.ignore(ignored);
  return count;
}

bool
pop3::_command(const string& cmd, bool ok)
{
  write(cmd + "\r\n");
  LOG("S: " << cmd << '\n');
  return _ok(ok);
}

bool
pop3::_ok(bool ok)
{
  string line = _readline();
  bool resp = line.substr(0, line.find_first_of(' ')) == "+OK";
  if (ok && !resp) throw mailbox::error(line);
  return resp;
}

string
pop3::_readline()
{
  string line = readline();
  if (line.empty()) throw mailbox::error("socket error: EOF");
  line.resize(line.size() - 2); // remove CRLF
  LOG("R: " << line << endl);
  return line;
}

pop3::plist
pop3::_plist()
{
  plist result;
  for (;;) {
    string line = _readline();
    if (!line.empty() && line[0] == '.') {
      line.assign(line, 1, line.size() - 1);
      if (line.empty()) break;
    }
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
  string line = _readline();
  for (; !line.empty(); line = _readline()) {
    if (line[0] == '.') {
      if (line.size() == 1) break;
      line.assign(line, 1, line.size() - 1);
    }
    result += line + "\r\n";
  }
  while (line.size() != 1 || line[0] != '.') line = _readline();
  return result;
}

mailbox::backend*
pop3tcp(const string& host, const string& port)
{
  auto_ptr<mailbox::backend> be(new pop3);
  be->tcp(host, port.empty() ? "110" : port);
  return be.release();
}

mailbox::backend*
pop3ssl(const string& host, const string& port)
{
  auto_ptr<mailbox::backend> be(new pop3);
  be->ssl(host, port.empty() ? "995" : port);
  return be.release();
}
