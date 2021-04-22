/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "win32.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

#if _DEBUG >= 2
#include <iostream>
#define DBG(s) s
#define LOG(s) (std::cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** imap4 - imap4 protocol backend
 * This class is a mailbox::backend for IMAP4 protocol.
 */
class imap4 : public mailbox::backend {
  unsigned _seq = _seqinit(); // sequencial number for the tag.

  // parser - imap4 response parser.
  struct parser : public tokenizer {
    parser() {}
    parser(std::string const& s) : tokenizer(s) {}
    std::string token(bool open = false);
  };

  // response - imap4 response type.
  struct response { std::string tag, type, data; };

  static std::string _utf7m(std::string_view s);
  std::string _tag();
  static std::string _arg(std::string_view arg);
  std::string _command(std::string_view cmd, std::string_view res = {});
  response _response();
  std::string _read();
  unsigned _seqinit() const { return unsigned(ptrdiff_t(this)) + unsigned(time({})); }
#ifdef _DEBUG
  using backend::read;
  std::string read() {
    auto line = backend::read();
    LOG("R: " << line << std::endl);
    return line;
  }
#endif
public:
  void login(uri const& uri, std::string const& passwd) override;
  void logout() override;
  size_t fetch(mailbox& mbox, uri const& uri) override;
};

void
imap4::login(uri const& uri, std::string const& passwd)
{
  constexpr char notimap[] = "server not IMAP4 compliant";
  auto resp = _response();
  auto preauth = resp.type == "PREAUTH";
  if (resp.tag != "*" || (!preauth && resp.type != "OK")) throw mailbox::error(notimap);
  auto imap = false;
  auto stls = false;
  constexpr char CAPABILITY[] = "CAPABILITY", STARTTLS[] = "STARTTLS";
  auto cap = _command(CAPABILITY, CAPABILITY);
  for (parser caps(cap); caps;) {
    if (auto s = caps.token(); s == "IMAP4" || s == "IMAP4REV1") imap = true;
    else if (s == STARTTLS) stls = true;
  }
  if (!imap) throw mailbox::error(notimap);
  if (preauth) return;
  if (stls && !tls()) {
    _command(STARTTLS);
    starttls(uri[uri::host]);
    cap = _command(CAPABILITY, CAPABILITY);
  }
  for (parser caps(cap); caps;) {
    if (caps.token() == "LOGINDISABLED") throw mailbox::error("login disabled");
  }
  _command("LOGIN" + _arg(uri[uri::user]) + _arg(passwd));
}

void
imap4::logout()
{
  _command("LOGOUT");
}

size_t
imap4::fetch(mailbox& mbox, uri const& uri)
{
  auto& path = uri[uri::path];
  _command("EXAMINE" + _arg(!path.empty() ? _utf7m(path) : "INBOX"));
  std::list<mail> mails, recents;
  for (parser ids(_command("UID SEARCH UNSEEN", "SEARCH")); ids;) {
    auto uid = ids.token();
    if (auto p = mbox.find(uid); p) {
      mails.push_back(*p);
      continue;
    }
    LOG("Fetch mail: " << uid << std::endl);
    parser parse(_command("UID FETCH " + uid +
			  " BODY.PEEK[HEADER.FIELDS (SUBJECT FROM DATE)]",
			  "FETCH"));
    parse.token(); // drop sequence#
    if (parse.peek() != '(') throw mailbox::error(parse.data());
    for (parse = parse.token(true); parse;) {
      auto item = parse.token(), value = parse.token();
      if (item.starts_with("BODY[HEADER.FIELDS (")) {
	mail m(uid);
	m.header(value);
	recents.push_back(m);
	break;
      }
    }
  }
  auto count = recents.size();
  mails.splice(mails.end(), recents);
  mbox.mails(mails);
  return count;
}

std::string
imap4::_utf7m(std::string_view s)
{
  constexpr auto avail = [](int c) { return c >= ' ' && c <= '~' && c != '&'; };
  size_t i = 0;
  while (i < s.size() && avail(s[i])) ++i;
  if (i == s.size()) return std::string(s);

  // encode path by modified UTF-7.
  auto result = std::string(s.substr(0, i));
  auto ws = win32::wstring(s.substr(i));
  auto const end = ws.cend();
  for (auto p = ws.cbegin(); p != end;) {
    result += '&';
    if (*p != '&') {
      constexpr char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+,";
      unsigned wc = 0;
      int n = 0;
      while (p != end && !avail(*p)) {
	wc = (wc << 16) + *p++, n += 16;
	for (; n >= 6; n -= 6) result += b64[(wc >> (n - 6)) & 63];
      }
      if (n) result += b64[(wc << (6 - n)) & 63];
    } else ++p;
    result += '-';
    while (p != end && avail(*p)) result += static_cast<char>(*p++);
  }
  return result;
}

std::string
imap4::_tag()
{
  auto n = _seq++;
  char s[4];
  s[3] = '0' + n % 10, n /= 10;
  for (int i = 3; i--;) s[i] = 'A' + (n & 15), n >>= 4;
  return { s, 4 };
}

std::string
imap4::_arg(std::string_view arg)
{
  if (!arg.empty() && [](auto arg) {
    constexpr std::string_view ec("(){%*\"\\");
    for (auto c : arg) {
      if (c <= 32 || c >= 127 || ec.find(c) != ec.npos) return false;
    }
    return true;
  }(arg)) return ' ' + std::string(arg);
  std::string esc(" \"");
  while (!arg.empty()) {
    auto i = arg.find_first_of("\"\\");
    esc += arg.substr(0, i);
    if (i == arg.npos) break;
    char const qst[] { '\\', arg[i] };
    esc.append(qst, 2);
    arg = arg.substr(i + 1);
  }
  return esc + '"';
}

std::string
imap4::_command(std::string_view cmd, std::string_view res)
{
  auto const tag = _tag();
  // send a command message to the server.
  write((tag + ' ').append(cmd));
  LOG("S: " << tag << " " << cmd << std::endl);

  response resp;
  std::string untagged;
  auto bye = false;
  for (;;) {
    resp = _response();
    if (!res.empty() && resp.type == "OK") {
      if (parser parse(resp.data); parse.peek() == '[') {
	parse = parse.token(true);
	if (parse.token() == res) untagged = parse.remain();
      }
    }
    if (resp.tag != "*") break;
    if (resp.type == "BYE") bye = true;
    if (!res.empty() && resp.type == res) untagged = resp.data;
  }
  if (resp.tag != tag) throw mailbox::error("unexpected tagged response");
  if (bye && cmd != "LOGOUT") throw mailbox::error("bye");
  if (resp.type != "OK") throw mailbox::error(resp.type + ' ' + resp.data);
  return untagged;
}

imap4::response
imap4::_response()
{
  parser parse(_read());
  response resp;
  if (resp.tag = parse.token(); resp.tag == "+") { // continuation
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

std::string
imap4::_read()
{
  auto line = read();
  if (!line.empty() && line[0] != '+') {
    while (line[line.size() - 1] == '}') {
      auto i = line.find_last_of('{');
      if (i == line.npos) break;
      auto p = line.c_str();
      char* end;
      auto size = strtoul(p + i + 1, &end, 10);
      if (static_cast<size_t>(end - p) != line.size() - 1) break;
      if (size) { // read literal data.
	auto literal = read(size);
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
std::string
imap4::parser::token(bool open)
{
  if (!*this) {};
  char delim[] = " [(\"{";
  auto i = findf(delim);
  auto result = uppercase(i);
  if (i == _s.npos) {
    _next = _s.size();
  } else if (_s[i] == ' ') {
    _next = i + 1;
  } else if (_s[i] == '[' || result.empty()) {
    result += _s[i];
    for (std::string st(1, _s[i++]); !st.empty();) {
      _next = i;
      if (_next >= _s.size()) throw mailbox::error("invalid token: " + _s);
      auto sp = st.size() - 1;
      switch (st[sp]) {
      case '"':
	if (i = findq("\"", i); i != _s.npos) {
	  result.append(_s, _next, ++i - _next);
	  st.erase(sp);
	}
	break;
      case '{':
	{
	  auto p = _s.c_str();
	  char* end;
	  i = strtoul(p + i, &end, 10);
	  i += std::string::size_type(end - p) + 1;
	  if (*end == '}' && i <= _s.size()) {
	    result.append(_s, _next, i - _next);
	    st.erase(sp);
	  } else {
	    i = _s.npos;
	  }
	}
	break;
      default:
	delim[0] = st[sp] == '[' ? ']' : ')';
	if (i = findf(delim, i); i != _s.npos) {
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
	if (!open || (result[0] != '(' && result[0] != '[')) break;
      case '"':
	result.assign(result, 1, result.size() - 2);
	break;
      case '{':
	assert(result.find('}') != result.npos);
	result.erase(0, result.find('}') + 1);
	break;
      }
    }
  }
  return result;
}

mailbox::backend* backendIMAP4() { return new imap4; }
