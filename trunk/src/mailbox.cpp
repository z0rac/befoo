/*
 * Copyright (C) 2009-2010 TSUBAKIMOTO Hiroya <zorac@4000do.co.jp>
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
  public:
    ~tcpstream() { _socket.shutdown(); }
    void open(const string& host, const string& port, int domain);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const;
    mailbox::backend::stream* starttls();
  };
}

void
tcpstream::open(const string& host, const string& port, int domain)
{
  assert(_socket == INVALID_SOCKET);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
}

void
tcpstream::close()
{
  _socket.shutdown();
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
#if USE_OPENSSL
#include "win32.h"
namespace {
  extern "C" {
    typedef struct { int ssl; } SSL;
    typedef struct { int ctx; } SSL_CTX;
    typedef struct { int method; } SSL_METHOD;

    typedef int (*SSL_library_init)(void);
    typedef SSL_CTX* (*SSL_CTX_new)(SSL_METHOD*);
    typedef void (*SSL_CTX_free)(SSL_CTX*);
    typedef SSL_METHOD* (*SSLv23_client_method)(void);
    typedef SSL_METHOD* (*TLSv1_client_method)(void);
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

  class sslstream : public mailbox::backend::stream {
    SSL_CTX* _ctx;
    SSL* _ssl;
    SSL_read _read;
    SSL_write _write;
    winsock::tcpclient _socket;
    int _want(int rc);
    void _connect();
    void _shutdown();
  public:
    sslstream();
    ~sslstream();
    void open(SOCKET socket);
    void open(const string& host, const string& port, int domain);
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

sslstream::sslstream()
  : _ssl(NULL), _read(SSL(SSL_read)), _write(SSL(SSL_write))
{
#if USE_SSL2
  _ctx = SSL(SSL_CTX_new)(SSL(SSLv23_client_method)());
#else
  _ctx = SSL(SSL_CTX_new)(SSL(TLSv1_client_method)());
#endif
  if (!_ctx) throw error();
}

sslstream::~sslstream()
{
  _shutdown();
  XSSL(SSL_CTX_free)(_ctx);
}

int
sslstream::_want(int rc)
{
  if (rc > 0) return rc;
  switch (SSL(SSL_get_error)(_ssl, rc)) {
  case 2: /* SSL_ERROR_WANT_READ */ return rc;
  case 3: /* SSL_ERROR_WANT_WRITE */ return rc;
  }
  throw error();
}

void
sslstream::_connect()
{
  assert(_socket != INVALID_SOCKET && !_ssl);
  try {
    _ssl = SSL(SSL_new)(_ctx);
    if (!_ssl ||
	SSL(SSL_set_fd)(_ssl, _socket) != 1 ||
	SSL(SSL_connect)(_ssl) != 1) throw error();
  } catch (...) {
    if (_ssl) XSSL(SSL_free)(_ssl), _ssl = NULL;
    throw;
  }
}

void
sslstream::_shutdown()
{
  if (_ssl) {
    XSSL(SSL_shutdown)(_ssl);
    XSSL(SSL_free)(_ssl);
    _ssl = NULL;
  }
}

void
sslstream::open(SOCKET socket)
{
  assert(_socket == INVALID_SOCKET);
  _socket(socket).timeout(TCP_TIMEOUT);
  _connect();
}

void
sslstream::open(const string& host, const string& port, int domain)
{
  assert(_socket == INVALID_SOCKET);
  _socket.connect(host, port, domain).timeout(TCP_TIMEOUT);
  _connect();
}

void
sslstream::close()
{
  _shutdown();
  _socket.shutdown();
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
  for (;;) {
    int n = _want(_write(_ssl, data, size));
    if (n > 0) return size_t(n);
  }
}

int
tcpstream::tls() const
{
  return sslstream::avail() ? 0 : -1;
}

#undef LIB
#undef XSSL
#undef SSL
#else // !USE_OPENSSL
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
  public:
    void open(SOCKET socket);
    void open(const string& host, const string& port, int domain);
    void close();
    size_t read(char* buf, size_t size);
    size_t write(const char* data, size_t size);
    int tls() const { return 1; }
    mailbox::backend::stream* starttls() { return NULL; }
  };
}

size_t
sslstream::tls::_recv(char* buf, size_t size)
{
  assert(socket != INVALID_SOCKET);
  for (;;) {
    size_t n = socket.recv(buf, size);
    if (n) return n;
    socket.wait(0, TCP_TIMEOUT);
  }
}

size_t
sslstream::tls::_send(const char* data, size_t size)
{
  assert(socket != INVALID_SOCKET);
  for (;;) {
    size_t n = socket.send(data, size);
    if (n) return n;
    socket.wait(1, TCP_TIMEOUT);
  }
}

void
sslstream::open(SOCKET socket)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket(socket).timeout(-1); // non-blocking
  _tls.connect();
}

void
sslstream::open(const string& host, const string& port, int domain)
{
  assert(_tls.socket == INVALID_SOCKET);
  _tls.socket.connect(host, port, domain).timeout(-1); // non-blocking
  _tls.connect().authenticate(host);
}

void
sslstream::close()
{
  _tls.shutdown();
  _tls.socket.shutdown();
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
#endif // !USE_OPENSSL

mailbox::backend::stream*
tcpstream::starttls()
{
  assert(_socket != INVALID_SOCKET);
  auto_ptr<sslstream> st(new sslstream);
  st->open(_socket.release());
  return st.release();
}

/*
 * Functions of the class mailbox::backend
 */
void
mailbox::backend::tcp(const string& host, const string& port, int domain)
{
  _st.reset(new tcpstream);
  _st->open(host, port, domain);
}

void
mailbox::backend::ssl(const string& host, const string& port, int domain)
{
  _st.reset(new sslstream);
  _st->open(host, port, domain);
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
void
mailbox::uripasswd(const string& uri, const string& passwd, int domain)
{
  _uri = uri;
  _passwd = passwd;
  _domain = domain;
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
  extern backend* imap4tcp(const string&, const string&, int);
  extern backend* imap4ssl(const string&, const string&, int);
  extern backend* pop3tcp(const string&, const string&, int);
  extern backend* pop3ssl(const string&, const string&, int);

  static const struct {
    const char* scheme;
    backend* (*make)(const string&, const string&, int);
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
      be.reset(backends[i].make(_uri[uri::host], _uri[uri::port], _domain));
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
