/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include <stack>

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

  // arg_t - argument type.
  struct arg_t : public string {
    arg_t() {}
    arg_t(const string& arg) : string(arg) {}
    arg_t(const char* arg) : string(arg) {}
    arg_t& operator()(const string& arg);
    arg_t& q(const string& arg);
  };

  // parse_t - imap4 response parser.
  struct parse_t : public tokenizer {
    parse_t() {}
    parse_t(const string& s) : tokenizer(s) {}
    string token(bool open = false);
  };

  // resp_t - response type.
  struct resp_t { string tag, type, data; };

  bool _initialize();
  string _tag();
  resp_t _command(const char* name,
		  const arg_t& args = arg_t(),
		  const char* res = NULL);
  resp_t _response();
  string _readline();
  unsigned _seqinit() const { return unsigned(this) + time(NULL); }
public:
  imap4() : _seq(_seqinit()) {}
  void login(const string& user, const string& passwd);
  void logout();
  int fetch(mailbox& mbox, const string& path);
};

void
imap4::login(const string& user, const string& passwd)
{
  if (!_initialize()) {
    resp_t resp = _command("LOGIN", arg_t().q(user).q(passwd));
    if (resp.type != "OK") throw mailbox::error(resp.data);
  }
}

void
imap4::logout()
{
  _command("LOGOUT");
}

int
imap4::fetch(mailbox& mbox, const string& path)
{
  resp_t resp = _command("EXAMINE", path.empty() ? "INBOX" : arg_t().q(path));
  if (resp.type != "OK") throw mailbox::error(resp.data);
  resp = _command("UID SEARCH", "UNSEEN", "SEARCH");
  if (resp.type != "SEARCH") throw mailbox::error(resp.data);
  list<mail> fetched;
  size_t copies = 0;
  for (parse_t ids = resp.data; ids;) {
    string uid = ids.token();
    const mail* p = mbox.find(uid);
    if (p) {
      fetched.push_back(*p);
      ++copies;
      continue;
    }
    LOG("Fetch mail: " << uid << endl);
    static const char param[] = "BODY.PEEK[HEADER.FIELDS (SUBJECT FROM DATE)]";
    resp = _command("UID FETCH", arg_t(uid)(param), "FETCH");
    parse_t parse = resp.data;
    parse.token(); // drop sequence#
    if (resp.type != "FETCH" || parse.peek() != '(') {
      throw mailbox::error(resp.data);
    }
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

bool
imap4::_initialize()
{
  resp_t resp = _response();
  static const char PREAUTH[] = "PREAUTH";
  if (resp.tag == "*" && (resp.type == "OK" || resp.type == PREAUTH)) {
    bool auth = resp.type == PREAUTH;
    bool imap = false;
    bool stls = false;
    static const char CAPABILITY[] = "CAPABILITY";
    resp = _command(CAPABILITY, arg_t(), CAPABILITY);
    if (resp.type == CAPABILITY) {
      static const char STARTTLS[] = "STARTTLS";
      for (parse_t caps = resp.data; caps;) {
	string s = caps.token();
	if (s == "IMAP4" || s == "IMAP4REV1") imap = true;
	else if (s == STARTTLS) stls = true;
      }
      if (imap) {
	if (stls && !tls()) {
	  _command(STARTTLS);
	  starttls();
	}
	return auth;
      }
    }
  }
  throw mailbox::error("server not IMAP4 compliant");
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

imap4::resp_t
imap4::_command(const char* name, const arg_t& args, const char* res)
{
  string tag = _tag();
  { // send a command message to the server.
    string msg = tag + ' ' + name;
    if (!args.empty()) msg += ' ' + args;
    write(msg + "\r\n");
    LOG("S: " << msg << '\n');
  }
  resp_t resp;
  string untagged;
  bool bye = false;
  for (;;) {
    resp = _response();
    if (res && resp.type == "OK") {
      parse_t parse = resp.data;
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
  if (bye && stricmp(name, "LOGOUT") != 0) throw mailbox::error("bye");
  if (resp.type == "BAD") {
    throw mailbox::error(string(name) + " error: BAD " + resp.data);
  }
  if (res && resp.type == "OK") {
    resp.type = res, resp.data = untagged;
  }
  return resp;
}

imap4::resp_t
imap4::_response()
{
  resp_t resp;
  parse_t parse = _readline();
  resp.tag = parse.token();
  if (resp.tag == "+") { // continuation
    resp.data = parse.remain();
    return resp;
  }
  resp.type = parse.token();
  if (resp.tag.empty() || resp.type.empty()) {
    throw mailbox::error("unexpected response: " + parse.data());
  }
  if (parse && resp.type.find_first_not_of("0123456789") == string::npos) {
    resp.data = resp.type, resp.type = parse.token();
  }
  if (parse) {
    if (!resp.data.empty()) resp.data += ' ';
    resp.data += parse.remain();
  }
  return resp;
}

string
imap4::_readline()
{
  string line = readline();
  if (line.empty()) throw mailbox::error("socket error: EOF");
  line.resize(line.size() - 2); // remove CRLF
  LOG("R: " << line << endl);
  if (line.size() > 1 && (line[0] != '+' || line[1] != ' ')) {
    while (*line.rbegin() == '}') {
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
      { // read a following line.
	string follow = readline();
	follow.resize(follow.size() - 2); // remove CRLF
	LOG(follow << endl);
	line += follow;
      }
    }
  }
  return line;
}

/*
 * Functions of the class imap4::arg_t
 */
imap4::arg_t&
imap4::arg_t::operator()(const string& arg)
{
  if (!arg.empty()) append(empty() ? arg : ' ' + arg);
  return *this;
}

imap4::arg_t&
imap4::arg_t::q(const string& arg)
{
  string esc;
  char qst[] = "\\ ";
  string::size_type i = 0, n;
  for (; (n = arg.find_first_of("\"\\", i)) != string::npos; i = n + 1) {
    qst[1] = arg[n];
    esc.append(arg, i, n - i).append(qst, 2);
  }
  return operator()('"' + esc + arg.substr(i) + '"');
}

/*
 * Functions of the class imap4::parse_t
 */
string
imap4::parse_t::token(bool open)
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
      stack<char> st;
      result += _s[i];
      for (st.push(_s[i++]); !st.empty();) {
	_next = i;
	if (_next >= _s.size()) {
	  throw mailbox::error("invalid token: " + _s);
	}
	switch (st.top()) {
	case '"':
	  i = findq("\"", i);
	  if (i == string::npos) break;
	  result.append(_s, _next, ++i - _next);
	  st.pop();
	  break;
	case '{':
	  {
	    const char* p = _s.c_str();
	    char* end;
	    i = strtoul(p + i, &end, 10);
	    i += string::size_type(end - p) + 1;
	    if (*end == '}' && i <= _s.size()) {
	      result.append(_s, _next, i - _next);
	      st.pop();
	    } else {
	      i = string::npos;
	    }
	  }
	  break;
	default:
	  delim[0] = st.top() == '[' ? ']' : ')';
	  i = findf(delim, i);
	  if (i == string::npos) break;
	  if (_s[i] == delim[0]) st.pop();
	  else st.push(_s[i]);
	  result += uppercase(++i);
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
	  i = result.find_first_of('}');
	  if (i != string::npos) result = result.substr(i + 1);
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
