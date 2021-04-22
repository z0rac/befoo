/*
 * Copyright (C) 2009-2016 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "winsock.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

#define TCP_TIMEOUT 60

/** tcpstream - stream of TCP session.
 * This instance should be created by the function mailbox::backend::tcp.
 */
namespace {
  class tcpstream : public mailbox::backend::stream {
    winsock::tcpclient _socket;
    int _verifylevel;
  public:
    tcpstream(int verifylevel) : _verifylevel(verifylevel) {}
    ~tcpstream() { _socket.shutdown(); }
    void connect(const string& host, const string& port, int domain);
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls(const string& host);
  };
}

void
tcpstream::connect(const string& host, const string& port, int domain)
{
  assert(_socket == INVALID_SOCKET);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
}

size_t
tcpstream::read(char* buf, size_t size)
{
  return _socket.recv(buf, size);
}

size_t
tcpstream::write(const char* data, size_t size)
{
  return _socket.send(data, size);
}

/** sslstream - stream of SSL session.
 * This instance should be created by the function mailbox::backend::ssl().
 */
namespace {
  class sslstream : public mailbox::backend::stream {
    struct tls : public winsock::tlsclient {
      winsock::tcpclient socket;
#if USE_SSL2
      tls() : winsock::tlsclient(SP_PROT_SSL2 | SP_PROT_SSL3) {}
#endif
      ~tls() { shutdown(); }
      size_t _recv(char* buf, size_t size);
      size_t _send(const char* data, size_t size);
    };
    tls _tls;
    int _verifylevel;
    void _connect(const string& host);
  public:
    sslstream(int verifylevel) : _verifylevel(verifylevel) {}
    void connect(SOCKET socket, const string& host);
    void connect(const string& host, const string& port, int domain);
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls(const string&) { return NULL; }
  };
}

size_t
sslstream::tls::_recv(char* buf, size_t size)
{
  assert(socket != INVALID_SOCKET);
  return socket.recv(buf, size);
}

size_t
sslstream::tls::_send(const char* data, size_t size)
{
  assert(socket != INVALID_SOCKET);
  return socket.send(data, size);
}

void
sslstream::_connect(const string& host)
{
  _tls.connect();
  if (_verifylevel) {
    DWORD ignore = 0;
    switch (_verifylevel) {
    case 1: ignore |= (SECURITY_FLAG_IGNORE_REVOCATION |
		       SECURITY_FLAG_IGNORE_WRONG_USAGE |
		       SECURITY_FLAG_IGNORE_CERT_DATE_INVALID);
    case 2: ignore |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    }
    if (!_tls.verify(host, ignore)) throw mailbox::error("invalid host");
  }
}

void
sslstream::connect(SOCKET socket, const string& host)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket(socket).timeout(TCP_TIMEOUT);
  _connect(host);
}

void
sslstream::connect(const string& host, const string& port, int domain)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
  _connect(host);
}

size_t
sslstream::read(char* buf, size_t size)
{
  return _tls.recv(buf, size);
}

size_t
sslstream::write(const char* data, size_t size)
{
  return _tls.send(data, size);
}

int
tcpstream::tls() const
{
  return 0;
}

mailbox::backend::stream*
tcpstream::starttls(const string& host)
{
  assert(_socket != INVALID_SOCKET);
  unique_ptr<sslstream> st(new sslstream(_verifylevel));
  st->connect(_socket.release(), host);
  return st.release();
}

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(const string& host, const string& port, int domain, int verify)
{
  unique_ptr<tcpstream> st(new tcpstream(verify));
  st->connect(host, port, domain);
  _st.reset(st.release());
}

void
mailbox::backend::ssl(const string& host, const string& port, int domain, int verify)
{
  unique_ptr<sslstream> st(new sslstream(verify));
  st->connect(host, port, domain);
  _st.reset(st.release());
}

void
mailbox::backend::starttls(const string& host)
{
  stream* st = _st->starttls(host);
  if (st) _st.reset(st);
}

string
mailbox::backend::read(size_t size)
{
  string result;
  while (size) {
    char buf[1024];
    size_t n = _st->read(buf, min(size, sizeof(buf)));
    if (!n) throw mailbox::error("connection reset");
    result.append(buf, n);
    size -= n;
  }
  return result;
}

string
mailbox::backend::read()
{
  char c;
  if (!_st->read(&c, 1)) throw mailbox::error("connection reset");
  string result(1, c);
  do {
    do {
      if (!_st->read(&c, 1)) throw mailbox::error("connection reset");
      result.push_back(c);
    } while (c != '\012');
  } while (result[result.size() - 2] != '\015');
  return result.erase(result.size() - 2); // remove CRLF
}

void
mailbox::backend::write(const char* data, size_t size)
{
  while (size) {
    size_t n = _st->write(data, size);
    data += n, size -= n;
  }
}

void
mailbox::backend::write(const string& data)
{
  string ln(data);
  ln.append("\015\012");
  write(ln.data(), ln.size());
}

/*
 * Functions of the class mailbox
 */
mailbox&
mailbox::uripasswd(const string& uri, const string& passwd)
{
  _uri = ::uri(uri);
  _passwd = passwd;
  return *this;
}

const mail*
mailbox::find(const string& uid) const
{
  list<mail>::const_iterator p = _mails.begin();
  for (; p != _mails.end(); ++p) {
    if (p->uid() == uid) return &*p;
  }
  return NULL;
}

void
mailbox::fetchmail()
{
  extern backend* backendIMAP4();
  extern backend* backendPOP3();

  static const struct {
    const char* scheme;
    backend* (*make)();
    void (backend::*stream)(const string&, const string&, int, int);
    const char* port;
  } backends[] = {
    { "imap",     backendIMAP4, &backend::tcp, "143" },
    { "imap+ssl", backendIMAP4, &backend::ssl, "993" },
    { "pop",      backendPOP3,  &backend::tcp, "110" },
    { "pop+ssl",  backendPOP3,  &backend::ssl, "995" },
  };

  _recent = -1;
  int i = sizeof(backends) / sizeof(*backends);
  while (i-- && _uri[uri::scheme] != backends[i].scheme) continue;
  if (i < 0) throw error("invalid scheme");

  uri u(_uri);
  string pw(_passwd);
  if (u[uri::port].empty()) u[uri::port] = backends[i].port;
  if (u[uri::user].empty()) {
    u[uri::user] = "ANONYMOUS";
    if (pw.empty()) pw = "befoo@";
  }
  unique_ptr<backend> be(backends[i].make());
  ((*be).*backends[i].stream)(u[uri::host], u[uri::port], _domain, _verify);
  be->login(u, pw);
  _recent = static_cast<int>(be->fetch(*this, u));
  be->logout();
}
