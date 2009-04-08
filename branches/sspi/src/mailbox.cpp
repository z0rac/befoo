/*
 * Copyright (C) 2009 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
 *
 * This software comes with ABSOLUTELY NO WARRANTY; for details of
 * the license terms, see the LICENSE.txt file included with the program.
 */
#include "mailbox.h"
#include "win32.h"
#include <cassert>

#ifdef _DEBUG
#include <iostream>
#define DBG(s) s
#define LOG(s) (cout << s)
#else
#define DBG(s)
#define LOG(s)
#endif

/** tcpstream - stream of TCP session.
 * This instance should be created by the function mailbox::backend::tcp.
 */
#define TCP_TIMEOUT 60
namespace {
  class tcpstream : public mailbox::backend::stream {
    SOCKET _fd;
  protected:
    void _wait(int op = -1) const;
  public:
    tcpstream(SOCKET fd = INVALID_SOCKET) : _fd(fd) {}
    ~tcpstream();
    int open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls();
  };
}

tcpstream::~tcpstream()
{
  close();
}

void
tcpstream::_wait(int op) const
{
  fd_set fds[2];
  fd_set* fdsp[2] = { NULL, NULL };
  for (int i = 0; i < 2; ++i) {
    if (op >> 1 || op == i) {
      fdsp[i] = fds + i;
      FD_ZERO(fdsp[i]);
      FD_SET(_fd, fdsp[i]);
    }
  }
  timeval tv = { TCP_TIMEOUT, 0 };
  int n = select(_fd + 1, fdsp[0], fdsp[1], NULL, &tv);
  if (n <= 0) throw winsock::error(n ? h_errno : WSAETIMEDOUT);
}

int
tcpstream::open(const string& host, const string& port)
{
  assert(_fd == INVALID_SOCKET);
  LOG("Connect: " << host << ":" << port << endl);
  struct addrinfo* ai = winsock::getaddrinfo(host, port);
  for (struct addrinfo* p = ai; p; p = p->ai_next) {
    SOCKET fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == INVALID_SOCKET) continue;
    u_long nb = 1;
    if (connect(fd, p->ai_addr, p->ai_addrlen) == 0 &&
	ioctlsocket(fd, FIONBIO, &nb) == 0) {
      _fd = fd;
      break;
    }
    closesocket(fd);
  }
  winsock::freeaddrinfo(ai);
  if (_fd == INVALID_SOCKET) throw winsock::error();
  return _fd;
}

void
tcpstream::close()
{
  if (_fd != INVALID_SOCKET) {
    shutdown(_fd, SD_BOTH);
    char buf[32];
    while (recv(_fd, buf, sizeof(buf), 0) > 0) continue;
    closesocket(_fd);
    _fd = INVALID_SOCKET;
  }
}

size_t
tcpstream::read(char* buf, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  for (;;) {
    int n = recv(_fd, buf, size, 0);
    if (n > 0) return size_t(n);
    else if (n == 0 || h_errno != WSAEWOULDBLOCK) throw winsock::error();
    _wait(0);
  }
}

size_t
tcpstream::write(const char* data, size_t size)
{
  assert(_fd != INVALID_SOCKET);
  for (size_t done = 0; done < size;) {
    int n = send(_fd, data + done, size - done, 0);
    if (n > 0) done += n;
    else if (n == 0 || h_errno != WSAEWOULDBLOCK) throw winsock::error();
    else _wait(1);
  }
  return size;
}

/** sslstream - stream of SSL session.
 * This instance should be created by the function mailbox::backend::ssl().
 */
namespace {
  extern "C" {
    typedef struct { int ssl; } SSL;
    typedef struct { int ctx; } SSL_CTX;
    typedef struct { int method; } SSL_METHOD;

    typedef int (*SSL_library_init)(void);
    typedef SSL_CTX* (*SSL_CTX_new)(SSL_METHOD*);
    typedef void (*SSL_CTX_free)(SSL_CTX*);
    typedef SSL_METHOD* (*SSLv23_client_method)(void);
    typedef SSL* (*SSL_new)(SSL_CTX*);
    typedef void (*SSL_free)(SSL*);
    typedef int (*SSL_set_fd)(SSL*, int);
    typedef int (*SSL_connect)(SSL*);
    typedef int (*SSL_read)(SSL*, void*, int);
    typedef int (*SSL_write)(SSL*, const void*, int);
    typedef int (*SSL_shutdown)(SSL*);
    typedef int (*SSL_get_error)(SSL*, int);

    typedef unsigned long (*ERR_get_error)(void);
    typedef char* (*ERR_error_string)(unsigned long, char*);
  }

  struct openssl {
    win32::dll ssleay;
    win32::dll libeay;
    openssl();
    static void np() {}
  };
  openssl _openssl;

#define SSL(name) name(_openssl.ssleay(#name))
#define XSSL(name) name(_openssl.ssleay(#name, FARPROC(openssl::np)))
#define LIB(name) name(_openssl.libeay(#name))

  class sslstream : public tcpstream {
    SSL_CTX* _ctx;
    SSL* _ssl;
    SSL_read _read;
    SSL_write _write;
    int _want(int rc);
    int _connect(int fd);
  public:
    sslstream(int fd = -1);
    ~sslstream();
    int open(const string& host, const string& port);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls() { return NULL; }
    static bool avail() { return _openssl.ssleay && _openssl.libeay; }
  public:
    struct error : public mailbox::error {
      error() : mailbox::error(emsg()) {}
      static string emsg();
    };
    friend struct error;
  };
}

openssl::openssl()
  : ssleay("ssleay32.dll", false), libeay("libeay32.dll", false)
{
  if (ssleay && libeay) XSSL(SSL_library_init)();
}

string
sslstream::error::emsg()
{
  char buf[256];
  return LIB(ERR_error_string)(LIB(ERR_get_error)(), buf);
}

sslstream::sslstream(int fd)
  : tcpstream(fd), _ssl(NULL),
    _read(SSL(SSL_read)), _write(SSL(SSL_write))
{
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
  if (!_ctx) throw error();
  if (fd != -1) _connect(fd);
}

sslstream::~sslstream()
{
  close();
  XSSL(SSL_CTX_free)(_ctx);
}

int
sslstream::_want(int rc)
{
  if (rc > 0) return rc;
  switch (SSL(SSL_get_error)(_ssl, rc)) {
  case 2: /* SSL_ERROR_WANT_READ */ _wait(0); return rc;
  case 3: /* SSL_ERROR_WANT_WRITE */ _wait(1); return rc;
  }
  throw error();
}

int
sslstream::_connect(int fd)
{
  assert(!_ssl);
  try {
    _ssl = SSL(SSL_new)(_ctx);
    if (!_ssl || !SSL(SSL_set_fd)(_ssl, fd)) throw error();
    while (_want(SSL(SSL_connect)(_ssl)) < 0) continue;
  } catch (...) {
    tcpstream::close();
    if (_ssl) XSSL(SSL_free)(_ssl), _ssl = NULL;
    throw;
  }
  return fd;
}

int
sslstream::open(const string& host, const string& port)
{
  return _connect(tcpstream::open(host, port));
}

void
sslstream::close()
{
  if (_ssl) {
    XSSL(SSL_shutdown)(_ssl);
    XSSL(SSL_free)(_ssl);
    _ssl = NULL;
    tcpstream::close();
  }
}

size_t
sslstream::read(char* buf, size_t size)
{
  assert(_ssl);
  for (;;) {
    int n = _want(_read(_ssl, buf, size));
    if (n > 0) return size_t(n);
  }
}

size_t
sslstream::write(const char* data, size_t size)
{
  assert(_ssl);
  for (size_t done = 0; done < size;) {
    int n = _want(_write(_ssl, data + done, size - done));
    if (n > 0) done += n;
  }
  return size;
}

#undef LIB
#undef XSSL
#undef SSL

int
tcpstream::tls() const
{
  return sslstream::avail() ? 0 : -1;
}

mailbox::backend::stream*
tcpstream::starttls()
{
  assert(_fd != INVALID_SOCKET);
  SOCKET fd = _fd;
  _fd = INVALID_SOCKET;
  sslstream* st = new(nothrow) sslstream(fd);
  if (st) return st;
  _fd = fd;
  throw bad_alloc();
}

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(const string& host, const string& port)
{
  _st.reset(new tcpstream);
  _st->open(host, port);
}

void
mailbox::backend::ssl(const string& host, const string& port)
{
  _st.reset(new sslstream);
  _st->open(host, port);
}

void
mailbox::backend::starttls()
{
  stream* st = _st->starttls();
  if (st) _st.reset(st);
}

string
mailbox::backend::read(size_t size)
{
  string result;
  while (size) {
    char buf[1024];
    size_t n = _st->read(buf, min(size, sizeof(buf)));
    result.append(buf, n);
    size -= n;
  }
  return result;
}

string
mailbox::backend::read()
{
  char c;
  _st->read(&c, 1);
  string result(1, c);
  do {
    do {
      _st->read(&c, 1);
      result.push_back(c);
    } while (c != '\012');
  } while (result[result.size() - 2] != '\015');
  return result.erase(result.size() - 2); // remove CRLF
}

void
mailbox::backend::write(const string& data)
{
  string ln(data);
  ln.append("\015\012");
  write(ln.data(), ln.size());
}

/*
 * Functions of the class uri
 */
uri::operator string() const
{
  string s = _part[scheme] + "://";
  if (!_part[user].empty()) s += _part[user] + '@';
  s += _part[host];
  if (!_part[port].empty()) s += ':' + _part[port];
  s += '/' + _part[path];
  if (!_part[fragment].empty()) s += '#' + _part[fragment];
  return s;
}

void
uri::parse(const string& uri)
{
  for (int ui = scheme; ui <= fragment; ++ui) _part[ui].clear();

  string::size_type i = uri.find_first_of('#');
  if (i != string::npos) _part[fragment] = uri.substr(i + 1);

  string t(uri, 0, min(uri.find_first_of('?'), i));
  i = t.find("://");
  if (i != string::npos) {
    _part[scheme].assign(t, 0, i);
    t.erase(0, i + 3);
  }
  i = t.find_first_of('/');
  if (i != string::npos) {
    _part[path] = t.substr(i + 1);
    t.erase(i);
  }
  i = t.find_first_of('@');
  if (i != string::npos) {
    _part[user].assign(t, 0, min(t.find_first_of(';'), i));
    t.erase(0, i + 1);
  }
  i = t.find_last_not_of("0123456789");
  if (i != string::npos && t[i] == ':') {
    _part[port] = t.substr(i + 1);
    t.erase(i);
  }
  _part[host] = t;
}

/*
 * Functions of the class mailbox
 */
void
mailbox::uripasswd(const string& uri, const string& passwd)
{
  _uri.parse(uri);
  _passwd = passwd;
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
  extern backend* imap4tcp(const string&, const string&);
  extern backend* imap4ssl(const string&, const string&);
  extern backend* pop3tcp(const string&, const string&);
  extern backend* pop3ssl(const string&, const string&);

  static const struct {
    const char* scheme;
    backend* (*make)(const string&, const string&);
  } backends[] = {
    { "imap", imap4tcp },
    { "imap+ssl", imap4ssl },
    { "pop", pop3tcp },
    { "pop+ssl", pop3ssl }
  };

  _recent = -1;
  auto_ptr<backend> be;
  for (int i = 0; i < int(sizeof(backends) / sizeof(*backends)); ++i) {
    if (_uri[uri::scheme] == backends[i].scheme) {
      be.reset(backends[i].make(_uri[uri::host], _uri[uri::port]));
      break;
    }
  }
  if (!be.get()) throw error("invalid scheme");
  string u(_uri[uri::user]), pw(_passwd);
  if (u.empty()) {
    u = "ANONYMOUS";
    if (pw.empty()) pw = "befoo@";
  }
  be->login(u, pw);
  _recent = be->fetch(*this, _uri);
  be->logout();
}
