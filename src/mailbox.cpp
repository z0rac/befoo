/*
 * Copyright (C) 2009-2021 TSUBAKIMOTO Hiroya <z0rac@users.sourceforge.jp>
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
#define LOG(s) (std::cout << s)
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
    void connect(std::string const& host, std::string const& port, int domain);
    void disconnect() noexcept override { _socket.shutdown(); }
    size_t read(char* buf, size_t size) override;
    size_t write(char const* data, size_t size) override;
    bool tls() const noexcept override { return false; }
    mailbox::backend::stream* starttls(std::string const& host) override;
  };
}

void
tcpstream::connect(std::string const& host, std::string const& port, int domain)
{
  assert(!_socket);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
}

size_t
tcpstream::read(char* buf, size_t size)
{
  return _socket.recv(buf, size);
}

size_t
tcpstream::write(char const* data, size_t size)
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
      ~tls() { shutdown(); }
      bool availlo() const noexcept override { return static_cast<bool>(socket); }
      size_t recvlo(char* buf, size_t size) override { return socket.recv(buf, size); }
      size_t sendlo(char const* data, size_t size) override { return socket.send(data, size); }
    } _tls;
    int _verifylevel;
    void _connect(std::string const& host);
  public:
    sslstream(int verifylevel) : _verifylevel(verifylevel) {}
    void connect(SOCKET socket, std::string const& host);
    void connect(std::string const& host, std::string const& port, int domain);
    void disconnect() noexcept override { _tls.socket.shutdown(); }
    size_t read(char* buf, size_t size) override;
    size_t write(char const* data, size_t size) override;
    bool tls() const noexcept override { return true; }
    mailbox::backend::stream* starttls(std::string const&) override { return {}; }
  };
}

void
sslstream::_connect(std::string const& host)
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
sslstream::connect(SOCKET socket, std::string const& host)
{
  assert(!_tls.socket);
  _tls.socket = socket;
  _tls.socket.timeout(TCP_TIMEOUT);
  _connect(host);
}

void
sslstream::connect(std::string const& host, std::string const& port, int domain)
{
  assert(!_tls.socket);
  _tls.socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
  _connect(host);
}

size_t
sslstream::read(char* buf, size_t size)
{
  return _tls.recv(buf, size);
}

size_t
sslstream::write(char const* data, size_t size)
{
  return _tls.send(data, size);
}

mailbox::backend::stream*
tcpstream::starttls(std::string const& host)
{
  assert(_socket);
  std::unique_ptr<sslstream> st(new sslstream(_verifylevel));
  st->connect(_socket.release(), host);
  return st.release();
}

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(std::string const& host, std::string const& port, int domain, int verify)
{
  std::unique_ptr<tcpstream> st(new tcpstream(verify));
  st->connect(host, port, domain);
  _st.reset(st.release());
}

void
mailbox::backend::ssl(std::string const& host, std::string const& port, int domain, int verify)
{
  std::unique_ptr<sslstream> st(new sslstream(verify));
  st->connect(host, port, domain);
  _st.reset(st.release());
}

void
mailbox::backend::starttls(std::string const& host)
{
  stream* st = _st->starttls(host);
  if (st) _st.reset(st);
}

std::string
mailbox::backend::read(size_t size)
{
  std::string result;
  while (size) {
    char buf[1024];
    auto n = _st->read(buf, min(size, sizeof(buf)));
    result.append(buf, n);
    size -= n;
  }
  return result;
}

std::string
mailbox::backend::read()
{
  char c;
  try {
    _st->read(&c, 1);
  } catch (winsock::timedout const&) {
    throw silent();
  }
  std::string data(1, c);
  do {
    do {
      _st->read(&c, 1);
      data.push_back(c);
    } while (c != '\012');
  } while (data[data.size() - 2] != '\015');
  return data.substr(0, data.size() - 2); // remove CRLF
}

void
mailbox::backend::write(char const* data, size_t size)
{
  while (size) {
    size_t n = _st->write(data, size);
    data += n, size -= n;
  }
}

void
mailbox::backend::write(std::string const& data)
{
  std::string ln(data);
  ln.append("\015\012");
  write(ln.data(), ln.size());
}

/*
 * Functions of the class mailbox
 */
mailbox&
mailbox::uripasswd(std::string const& uri, std::string const& passwd)
{
  _uri = ::uri(uri);
  _passwd = passwd;
  return *this;
}

mail const*
mailbox::find(std::string const& uid) const
{
  for (auto& t : _mails) {
    if (t.uid() == uid) return &t;
  }
  return {};
}

void
mailbox::fetchmail(bool idle)
{
  extern backend* backendIMAP4();
  extern backend* backendPOP3();

  static const struct {
    char const* scheme;
    backend* (*make)();
    void (backend::*stream)(std::string const&, std::string const&, int, int);
    char const* port;
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
  std::string pw(_passwd);
  if (u[uri::port].empty()) u[uri::port] = backends[i].port;
  if (u[uri::user].empty()) {
    u[uri::user] = "ANONYMOUS";
    if (pw.empty()) pw = "befoo@";
  }
  std::unique_ptr<backend> be(backends[i].make());
  ((*be).*backends[i].stream)(u[uri::host], u[uri::port], _domain, _verify);
  struct exhibit {
    mailbox& mb;
    exhibit(mailbox& mb, backend* be) : mb(mb) { mb.lock(), mb._backend = be; }
    ~exhibit() { mb.lock(), mb._backend = {}; }
  } exhibit { *this, be.get() };
  fetching(false);
  idle = be->login(u, pw) && idle;
  _recent = static_cast<int>(be->fetch(*this, u));
  while (idle) {
    fetching(idle);
    try {
      _recent = static_cast<int>(be->fetch(*this));
    } catch (...) {
      _recent = -1;
      throw;
    }
  }
  be->logout();
}
