/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include <cassert>

#if _DEBUG >= 2
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** imap4 - imap4 protocol backend
 * This class is a mailbox::backend for IMAP4 protocol.
 */
class imap4 : public mailbox::backend {
  unsigned _seq; // sequencial number for the tag.

  // parser - imap4 response parser.
  struct parser : public tokenizer {
    parser() {}
    parser(const string& s) : tokenizer(s) {}
    string token(bool open = false);
  };

  // response - imap4 response type.
  struct response { string tag, type, data; };

  string _tag();
  static string _arg(const string& arg);
  string _command(const char* cmd, const char* res = NULL);
  string _command(const string& cmd, const char* res = NULL)
  { return _command(cmd.c_str(), res); }
  response _response();
  string _read();
  unsigned _seqinit() const { return unsigned(this) + unsigned(time(NULL)); }
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
  imap4() : _seq(_seqinit()) {}
  void login(const string& user, const string& passwd);
  void logout();
  int fetch(mailbox& mbox, const uri& uri);
};

void
imap4::login(const string& user, const string& passwd)
{
  static const char notimap[] = "server not IMAP4 compliant";
  response resp = _response();
  bool preauth = resp.type == "PREAUTH";
  if (resp.tag != "*" || !preauth && resp.type != "OK") {
    throw mailbox::error(notimap);
  }
  bool imap = false;
  bool stls = false;
  static const char CAPABILITY[] = "CAPABILITY";
  static const char STARTTLS[] = "STARTTLS";
  string cap = _command(CAPABILITY, CAPABILITY);
  for (parser caps(cap); caps;) {
    string s = caps.token();
    if (s == "IMAP4" || s == "IMAP4REV1") imap = true;
    else if (s == STARTTLS) stls = true;
  }
  if (!imap) throw mailbox::error(notimap);
  if (!preauth) {
    if (stls && !tls()) {
      _command(STARTTLS);
      starttls();
      cap = _command(CAPABILITY, CAPABILITY);
    }
    for (parser caps(cap); caps;) {
      if (caps.token() == "LOGINDISABLED") {
	throw mailbox::error("login disabled");
      }
    }
    _command("LOGIN" + _arg(user) + _arg(passwd));
  }
}

void
imap4::logout()
{
  _command("LOGOUT");
}

int
imap4::fetch(mailbox& mbox, const uri& uri)
{
  const string& path = uri[uri::path];
  _command("EXAMINE" + _arg(path.empty() ? "INBOX" : path));
  list<mail> fetched;
  size_t copies = 0;
  for (parser ids(_command("UID SEARCH UNSEEN", "SEARCH")); ids;) {
    string uid = ids.token();
    const mail* p = mbox.find(uid);
    if (p) {
      fetched.push_back(*p);
      ++copies;
      continue;
    }
    LOG("Fetch mail: " << uid << endl);
    parser parse(_command("UID FETCH " + uid +
			   " BODY.PEEK[HEADER.FIELDS (SUBJECT FROM DATE)]",
			   "FETCH"));
    parse.token(); // drop sequence#
    if (parse.peek() != '(') throw mailbox::error(parse.data());
    for (parse = parse.token(true); parse;) {
      string item = parse.token();
      string value = parse.token();
      if (item == "BODY[HEADER.FIELDS (SUBJECT FROM DATE)]") {
	mail m(uid);
	m.header(value);
	fetched.push_back(m);
	break;
      }
    }
  }
  mbox.mails(fetched);
  return int(fetched.size() - copies);
}

string
imap4::_tag()
{
  unsigned n = _seq++;
  char s[4];
  s[3] = '0' + n % 10, n /= 10;
  for (int i = 3; i--;) s[i] = 'A' + (n & 15), n >>= 4;
  return string(s, 4);
}

string
imap4::_arg(const string& arg)
{
  if (!arg.empty()) {
    string::const_iterator p = arg.begin();
    while (p != arg.end() &&
	   *p > 32 && *p < 127 && !strchr("(){%*\"\\", *p)) ++p;
    if (p == arg.end()) return ' ' + arg;
  }
  string esc(" \"");
  for (string::size_type i = 0;;) {
    string::size_type n = arg.find_first_of("\"\\", i);
    esc.append(arg, i, n - i);
    if (n == string::npos) break;
    char qst[] = { '\\', arg[n] };
    esc.append(qst, 2);
    i = n + 1;
  }
  return esc + '"';
}

string
imap4::_command(const char* cmd, const char* res)
{
  const string tag = _tag();
  // send a command message to the server.
  write(tag + ' ' + cmd);
  LOG("S: " << tag << " " << cmd << endl);

  response resp;
  string untagged;
  bool bye = false;
  for (;;) {
    resp = _response();
    if (res && resp.type == "OK") {
      parser parse(resp.data);
      if (parse.peek() == '[') {
	parse = parse.token(true);
	if (parse.token() == res) untagged = parse.remain();
      }
    }
    if (resp.tag != "*") break;
    if (resp.type == "BYE") bye = true;
    if (res && resp.type == res) untagged = resp.data;
  }
  if (resp.tag != tag) {
    throw mailbox::error("unexpected tagged response");
  }
  if (bye && _stricmp(cmd, "LOGOUT") != 0) throw mailbox::error("bye");
  if (resp.type != "OK") {
    throw mailbox::error(resp.type + ' ' + resp.data);
  }
  return untagged;
}

imap4::response
imap4::_response()
{
  parser parse(_read());
  response resp;
  resp.tag = parse.token();
  if (resp.tag == "+") { // continuation
    resp.data = parse.remain();
    return resp;
  }
  resp.type = parse.token();
  if (resp.tag.empty() || resp.type.empty()) {
    throw mailbox::error("unexpected response: " + parse.data());
  }
  if (parse && parser::digit(resp.type)) {
    resp.data = resp.type, resp.type = parse.token();
  }
  if (parse) {
    if (!resp.data.empty()) resp.data += ' ';
    resp.data += parse.remain();
  }
  return resp;
}

string
imap4::_read()
{
  string line = read();
  if (!line.empty() && line[0] != '+') {
    while (line[line.size() - 1] == '}') {
      string::size_type i = line.find_last_of('{');
      if (i == string::npos) break;
      const char* p = line.c_str();
      char* end;
      size_t size = strtoul(p + i + 1, &end, 10);
      if (string::size_type(end - p) != line.size() - 1) break;
      if (size) { // read literal data.
	string literal = read(size);
	LOG(literal);
	line += literal;
      }
      line += read(); // read a following line.
    }
  }
  return line;
}

/*
 * Functions of the class imap4::parser
 */
string
imap4::parser::token(bool open)
{
  string result;
  if (*this) {
    char delim[] = " [(\"{";
    string::size_type i = findf(delim);
    result = uppercase(i);
    if (i == string::npos) {
      _next = _s.size();
    } else if (_s[i] == ' ') {
      _next = i + 1;
    } else if (_s[i] == '[' || result.empty()) {
      result += _s[i];
      for (string st(1, _s[i++]); !st.empty();) {
	_next = i;
	if (_next >= _s.size()) {
	  throw mailbox::error("invalid token: " + _s);
	}
	string::size_type sp = st.size() - 1;
	switch (st[sp]) {
	case '"':
	  i = findq("\"", i);
	  if (i != string::npos) {
	    result.append(_s, _next, ++i - _next);
	    st.erase(sp);
	  }
	  break;
	case '{':
	  {
	    const char* p = _s.c_str();
	    char* end;
	    i = strtoul(p + i, &end, 10);
	    i += string::size_type(end - p) + 1;
	    if (*end == '}' && i <= _s.size()) {
	      result.append(_s, _next, i - _next);
	      st.erase(sp);
	    } else {
	      i = string::npos;
	    }
	  }
	  break;
	default:
	  delim[0] = st[sp] == '[' ? ']' : ')';
	  i = findf(delim, i);
	  if (i != string::npos) {
	    if (_s[i] == delim[0]) st.erase(sp);
	    else st.push_back(_s[i]);
	    result += uppercase(++i);
	  }
	  break;
	}
      }
      _next = i < _s.size() && _s[i] == ' ' ? i + 1 : i;
      if (result.size() > 1) {
	switch (result[0]) {
	default:
	  if (!open || result[0] != '(' && result[0] != '[') break;
	case '"':
	  result.assign(result, 1, result.size() - 2);
	  break;
	case '{':
	  assert(result.find_first_of('}') != string::npos);
	  result.erase(0, result.find_first_of('}') + 1);
	  break;
	}
      }
    }
  }
  return result;
}

mailbox::backend*
imap4tcp(const string& host, const string& port)
{
  auto_ptr<mailbox::backend> be(new imap4);
  be->tcp(host, port.empty() ? "143" : port);
  return be.release();
}

mailbox::backend*
imap4ssl(const string& host, const string& port)
{
  auto_ptr<mailbox::backend> be(new imap4);
  be->ssl(host, port.empty() ? "993" : port);
  return be.release();
}
